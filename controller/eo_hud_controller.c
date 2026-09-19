/*
 * ============================================================================
 * eo_hud_controller.c
 * ============================================================================
 *
 * Real-Time Electro-Optical Monitoring Platform
 *
 * Raspberry Pi 3 Model B+
 * TM4C123 Pan-Tilt Controller
 * Logitech C270 USB Camera
 * SSD1306-Compatible 128x64 I2C OLED
 *
 * --------------------------------------------------------------------------
 * FUNCTION
 * --------------------------------------------------------------------------
 *
 * This program integrates:
 *
 *   1. Raspberry Pi <-> TM4C123 UART communication
 *   2. PAN servo command/state
 *   3. TILT servo command/state
 *   4. USB camera presence monitoring
 *   5. Graphical electro-optical OLED HUD
 *
 * OLED displays live platform state:
 *
 *       - AZ = current PAN / azimuth angle
 *       - EL = current TILT / elevation angle
 *       - Moving PAN position pointer
 *       - EO aiming reticle
 *       - Camera status
 *       - UART link status
 *
 * --------------------------------------------------------------------------
 * IMPORTANT
 * --------------------------------------------------------------------------
 *
 * This display does not simulate radar distance.
 *
 * The reticle and PAN scale represent the real electro-optical
 * platform pointing state.
 *
 * Target detection state is received from the Raspberry Pi detection module.
 *
 * ============================================================================
 */


#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/select.h>

#include <linux/i2c-dev.h>


/* ============================================================================
 * HARDWARE CONFIGURATION
 * ========================================================================== */

#define SERIAL_DEVICE       "/dev/serial0"

#define OLED_DEVICE         "/dev/i2c-1"
#define OLED_ADDR           0x3C

#define CAMERA_DEVICE       "/dev/video0"
#define TARGET_STATE_FILE    "/tmp/eo_target_state"


/* ============================================================================
 * SERVO SAFE OPERATING LIMITS
 * ========================================================================== */

#define PAN_MIN_ANGLE       45
#define PAN_MAX_ANGLE       135

#define TILT_MIN_ANGLE      55
#define TILT_MAX_ANGLE      125

#define CENTER_ANGLE        90


/* ============================================================================
 * OLED CONFIGURATION
 * ========================================================================== */

#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_PAGES          8


/* ============================================================================
 * UART / INPUT BUFFERS
 * ========================================================================== */

#define RESPONSE_SIZE       128
#define COMMAND_SIZE        64
#define INPUT_SIZE          128


/* ============================================================================
 * GLOBAL PLATFORM STATE
 * ========================================================================== */

static int currentPan =
    CENTER_ANGLE;

static int currentTilt =
    CENTER_ANGLE;

static bool uartOnline =
    false;


/* ============================================================================
 * OLED STATE
 * ========================================================================== */

static int oledFd =
    -1;


/*
 * SSD1306 framebuffer:
 *
 * 128 columns x 8 pages = 1024 bytes.
 */

static uint8_t framebuffer[
    OLED_WIDTH * OLED_PAGES
];


/* ============================================================================
 * 5x7 FONT - LETTERS
 * ========================================================================== */

static const uint8_t fontLetters[26][5] =
{
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}  /* Z */
};


/* ============================================================================
 * 5x7 FONT - NUMBERS
 * ========================================================================== */

static const uint8_t fontDigits[10][5] =
{
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}  /* 9 */
};


/* ============================================================================
 * SPECIAL FONT SYMBOLS
 * ========================================================================== */

static const uint8_t glyphSpace[5] =
{
    0x00,0x00,0x00,0x00,0x00
};


static const uint8_t glyphColon[5] =
{
    0x00,0x36,0x36,0x00,0x00
};


static const uint8_t glyphDash[5] =
{
    0x08,0x08,0x08,0x08,0x08
};


/* ============================================================================
 * FONT LOOKUP
 * ========================================================================== */

static const uint8_t *GetGlyph(
    char c)
{
    if ((c >= 'A') &&
        (c <= 'Z'))
    {
        return
            fontLetters[c - 'A'];
    }


    if ((c >= '0') &&
        (c <= '9'))
    {
        return
            fontDigits[c - '0'];
    }


    switch (c)
    {
        case ':':
            return glyphColon;

        case '-':
            return glyphDash;

        default:
            return glyphSpace;
    }
}


/* ============================================================================
 * SSD1306 COMMAND
 * ========================================================================== */

static int OledCommand(
    uint8_t command)
{
    uint8_t buffer[2];


    buffer[0] =
        0x00;


    buffer[1] =
        command;


    if (write(
            oledFd,
            buffer,
            sizeof(buffer))
        != (ssize_t)sizeof(buffer))
    {
        return -1;
    }


    return 0;
}


/* ============================================================================
 * OLED INITIALIZATION
 * ========================================================================== */

static int OledInitialize(void)
{
    const uint8_t initSequence[] =
    {
        0xAE,

        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,

        0x8D, 0x14,

        0x20, 0x02,

        0xA1,
        0xC8,

        0xDA, 0x12,

        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x40,

        0xA4,
        0xA6,

        0xAF
    };


    size_t i;


    oledFd =
        open(
            OLED_DEVICE,
            O_RDWR
        );


    if (oledFd < 0)
    {
        perror(
            "Unable to open OLED"
        );


        return -1;
    }


    if (ioctl(
            oledFd,
            I2C_SLAVE,
            OLED_ADDR) < 0)
    {
        perror(
            "Unable to select OLED"
        );


        close(
            oledFd
        );


        oledFd =
            -1;


        return -1;
    }


    for (i = 0;
         i < sizeof(initSequence);
         i++)
    {
        if (OledCommand(
                initSequence[i]) != 0)
        {
            fprintf(
                stderr,
                "OLED initialization failed.\n"
            );


            return -1;
        }
    }


    return 0;
}


/* ============================================================================
 * FRAMEBUFFER
 * ========================================================================== */

static void ClearFramebuffer(void)
{
    memset(
        framebuffer,
        0,
        sizeof(framebuffer)
    );
}


/* ============================================================================
 * DRAW PIXEL
 * ========================================================================== */

static void DrawPixel(
    int x,
    int y)
{
    int index;

    uint8_t mask;


    if ((x < 0) ||
        (x >= OLED_WIDTH) ||
        (y < 0) ||
        (y >= OLED_HEIGHT))
    {
        return;
    }


    index =
        x +
        ((y / 8) *
         OLED_WIDTH);


    mask =
        (uint8_t)(
            1U << (y % 8)
        );


    framebuffer[index] |=
        mask;
}


/* ============================================================================
 * DRAW LINE
 * ========================================================================== */

static void DrawLine(
    int x0,
    int y0,
    int x1,
    int y1)
{
    int dx;
    int sx;
    int dy;
    int sy;
    int error;


    dx =
        abs(
            x1 - x0
        );


    sx =
        (x0 < x1)
        ? 1
        : -1;


    dy =
        -abs(
            y1 - y0
        );


    sy =
        (y0 < y1)
        ? 1
        : -1;


    error =
        dx + dy;


    while (1)
    {
        DrawPixel(
            x0,
            y0
        );


        if ((x0 == x1) &&
            (y0 == y1))
        {
            break;
        }


        {
            int e2 =
                2 * error;


            if (e2 >= dy)
            {
                error +=
                    dy;


                x0 +=
                    sx;
            }


            if (e2 <= dx)
            {
                error +=
                    dx;


                y0 +=
                    sy;
            }
        }
    }
}


/* ============================================================================
 * DRAW CHARACTER
 * ========================================================================== */

static void DrawCharacter(
    int x,
    int page,
    char c)
{
    const uint8_t *glyph;

    int i;


    if ((page < 0) ||
        (page >= OLED_PAGES))
    {
        return;
    }


    if ((x < 0) ||
        (x > (OLED_WIDTH - 6)))
    {
        return;
    }


    glyph =
        GetGlyph(c);


    for (i = 0;
         i < 5;
         i++)
    {
        framebuffer[
            (page * OLED_WIDTH) +
            x +
            i
        ] =
            glyph[i];
    }


    framebuffer[
        (page * OLED_WIDTH) +
        x +
        5
    ] =
        0x00;
}


/* ============================================================================
 * DRAW TEXT
 * ========================================================================== */

static void DrawText(
    int x,
    int page,
    const char *text)
{
    while (*text != '\0')
    {
        if (x >
            (OLED_WIDTH - 6))
        {
            break;
        }


        DrawCharacter(
            x,
            page,
            *text
        );


        x +=
            6;


        text++;
    }
}


/* ============================================================================
 * DEGREE SYMBOL
 * ========================================================================== */

static void DrawDegreeSymbol(
    int x,
    int y)
{
    DrawPixel(x,     y);
    DrawPixel(x + 1, y);

    DrawPixel(x,     y + 1);
    DrawPixel(x + 1, y + 1);
}


/* ============================================================================
 * STATUS DOT
 * ========================================================================== */

static void DrawStatusDot(
    int centerX,
    int centerY,
    bool active)
{
    int x;
    int y;

    /*
     * Active  : filled 5x5 marker
     * Inactive: hollow 5x5 marker
     *
     * This provides a clearly visible difference on the
     * 128x64 OLED compared with the previous 3x3 marker.
     */
    for (y = -2; y <= 2; y++)
    {
        for (x = -2; x <= 2; x++)
        {
            if (active)
            {
                DrawPixel(
                    centerX + x,
                    centerY + y
                );
            }
            else
            {
                if ((x == -2) ||
                    (x == 2) ||
                    (y == -2) ||
                    (y == 2))
                {
                    DrawPixel(
                        centerX + x,
                        centerY + y
                    );
                }
            }
        }
    }
}


/* ============================================================================
 * CAMERA PRESENCE
 * ========================================================================== */

/*
 * The current version checks whether Linux exposes /dev/video0.
 *
 * A later revision can replace this with actual uStreamer process
 * monitoring.
 */

static bool CameraConnected(void)
{
    return
        access(
            CAMERA_DEVICE,
            F_OK
        ) == 0;
}


/* ============================================================================
 * PAN ANGLE -> HUD SCALE POSITION
 * ========================================================================== */

static int PanToScaleX(
    int panAngle)
{
    const int leftX =
        18;


    const int rightX =
        110;


    if (panAngle <
        PAN_MIN_ANGLE)
    {
        panAngle =
            PAN_MIN_ANGLE;
    }


    if (panAngle >
        PAN_MAX_ANGLE)
    {
        panAngle =
            PAN_MAX_ANGLE;
    }


    return
        leftX +
        (
            (panAngle -
             PAN_MIN_ANGLE) *
            (rightX -
             leftX)
        ) /
        (
            PAN_MAX_ANGLE -
            PAN_MIN_ANGLE
        );
}



/* ============================================================================
 * TARGET DETECTION STATE
 * ========================================================================== */

static int TargetPresent(void)
{
    FILE *file;
    char state[32];

    file = fopen(
        TARGET_STATE_FILE,
        "r"
    );

    if (file == NULL)
    {
        return 0;
    }

    if (fgets(
            state,
            sizeof(state),
            file
        ) == NULL)
    {
        fclose(file);
        return 0;
    }

    fclose(file);

    if (
        strncmp(
            state,
            "TARGET_PRESENT",
            14
        ) == 0
        ||
        strncmp(
            state,
            "TARGET_ACQUIRED",
            15
        ) == 0
    )
    {
        return 1;
    }

    return 0;
}


/* ============================================================================
 * DRAW EO HUD
 * ========================================================================== */

static void DrawHud(void)
{
    char text[32];

    int pointerX;
    int targetPresent;


    ClearFramebuffer();

    targetPresent = TargetPresent();


    /* ---------------------------------------------------------------------
     * TITLE
     * ------------------------------------------------------------------ */

    DrawText(
        31,
        0,
        "EO PLATFORM"
    );


    /* ---------------------------------------------------------------------
     * PAN SCALE LABELS
     * ------------------------------------------------------------------ */

    DrawText(
        12,
        1,
        "45"
    );


    DrawText(
        58,
        1,
        "90"
    );


    DrawText(
        100,
        1,
        "135"
    );


    /* ---------------------------------------------------------------------
     * PAN SCALE
     * ------------------------------------------------------------------ */

    DrawLine(
        18,
        18,
        110,
        18
    );


    DrawLine(
        18,
        16,
        18,
        20
    );


    DrawLine(
        64,
        16,
        64,
        20
    );


    DrawLine(
        110,
        16,
        110,
        20
    );


    /* ---------------------------------------------------------------------
     * LIVE PAN POINTER
     * ------------------------------------------------------------------ */

    pointerX =
        PanToScaleX(
            currentPan
        );


    DrawLine(
        pointerX,
        21,
        pointerX - 3,
        24
    );


    DrawLine(
        pointerX,
        21,
        pointerX + 3,
        24
    );


    /* ---------------------------------------------------------------------
     * EO RETICLE
     * ------------------------------------------------------------------ */

    DrawLine(
        42,
        36,
        86,
        36
    );


    DrawLine(
        64,
        25,
        64,
        45
    );


    DrawLine(
        48,
        27,
        58,
        34
    );


    DrawLine(
        80,
        27,
        70,
        34
    );


    DrawLine(
        48,
        45,
        58,
        38
    );


    DrawLine(
        80,
        45,
        70,
        38
    );


    /*
     * Optical center / target marker.
     *
     * No target:
     *      small cross
     *
     * Target present:
     *      filled 3x3 target point
     */

    if (targetPresent)
    {
        int dx;
        int dy;

        for (dy = -1; dy <= 1; ++dy)
        {
            for (dx = -1; dx <= 1; ++dx)
            {
                DrawPixel(
                    64 + dx,
                    36 + dy
                );
            }
        }
    }
    else
    {
        DrawPixel(
            64,
            36
        );

        DrawPixel(
            63,
            36
        );

        DrawPixel(
            65,
            36
        );

        DrawPixel(
            64,
            35
        );

        DrawPixel(
            64,
            37
        );
    }


    /* ---------------------------------------------------------------------
     * LIVE AZIMUTH
     * ------------------------------------------------------------------ */

    snprintf(
        text,
        sizeof(text),
        "AZ %03d",
        currentPan
    );


    DrawText(
        0,
        6,
        text
    );


    DrawDegreeSymbol(
        38,
        49
    );


    /* ---------------------------------------------------------------------
     * LIVE ELEVATION
     * ------------------------------------------------------------------ */

    snprintf(
        text,
        sizeof(text),
        "EL %03d",
        currentTilt
    );


    DrawText(
        72,
        6,
        text
    );


    DrawDegreeSymbol(
        110,
        49
    );


    /* ---------------------------------------------------------------------
     * CAMERA STATUS
     * ------------------------------------------------------------------ */

    DrawText(
        0,
        7,
        "CAM"
    );


    DrawStatusDot(
        24,
        60,
        CameraConnected()
    );


    /* ---------------------------------------------------------------------
     * TARGET STATUS
     * ------------------------------------------------------------------ */

    DrawText(
        36,
        7,
        "TGT"
    );

    DrawStatusDot(
        60,
        60,
        targetPresent
    );


    /* ---------------------------------------------------------------------
     * UART STATUS
     * ------------------------------------------------------------------ */

    DrawText(
        70,
        7,
        "LINK"
    );


    DrawStatusDot(
        104,
        60,
        uartOnline
    );
}


/* ============================================================================
 * UPDATE PHYSICAL OLED
 * ========================================================================== */

static int OledUpdate(void)
{
    uint8_t page;
    uint8_t x;

    uint8_t packet[17];


    packet[0] =
        0x40;


    for (page = 0;
         page < OLED_PAGES;
         page++)
    {
        if (OledCommand(
                (uint8_t)(
                    0xB0 +
                    page
                )) != 0)
        {
            return -1;
        }


        if (OledCommand(
                0x00) != 0)
        {
            return -1;
        }


        if (OledCommand(
                0x10) != 0)
        {
            return -1;
        }


        for (x = 0;
             x < OLED_WIDTH;
             x += 16)
        {
            int i;


            for (i = 0;
                 i < 16;
                 i++)
            {
                packet[i + 1] =
                    framebuffer[
                        (page *
                         OLED_WIDTH) +
                        x +
                        i
                    ];
            }


            if (write(
                    oledFd,
                    packet,
                    sizeof(packet))
                != (ssize_t)sizeof(packet))
            {
                return -1;
            }
        }
    }


    return 0;
}


/* ============================================================================
 * REFRESH COMPLETE HUD
 * ========================================================================== */

static void RefreshHud(void)
{
    DrawHud();


    if (OledUpdate() != 0)
    {
        fprintf(
            stderr,
            "WARNING: OLED update failed.\n"
        );
    }
}


/* ============================================================================
 * UART CONFIGURATION
 * ========================================================================== */

static int ConfigureSerialPort(
    int fd)
{
    struct termios tty;


    if (tcgetattr(
            fd,
            &tty) != 0)
    {
        perror(
            "tcgetattr"
        );


        return -1;
    }


    cfmakeraw(
        &tty
    );


    if ((cfsetispeed(
            &tty,
            B115200) != 0) ||
        (cfsetospeed(
            &tty,
            B115200) != 0))
    {
        perror(
            "UART speed"
        );


        return -1;
    }


    tty.c_cflag &=
        ~CSIZE;


    tty.c_cflag |=
        CS8;


    tty.c_cflag &=
        ~PARENB;


    tty.c_cflag &=
        ~CSTOPB;


    tty.c_cflag |=
        CLOCAL |
        CREAD;


#ifdef CRTSCTS

    tty.c_cflag &=
        ~CRTSCTS;

#endif


    tty.c_iflag &=
        ~(
            IXON |
            IXOFF |
            IXANY
        );


    tty.c_cc[VMIN] =
        0;


    tty.c_cc[VTIME] =
        0;


    if (tcsetattr(
            fd,
            TCSANOW,
            &tty) != 0)
    {
        perror(
            "tcsetattr"
        );


        return -1;
    }


    (void)tcflush(
        fd,
        TCIOFLUSH
    );


    return 0;
}


/* ============================================================================
 * UART WRITE
 * ========================================================================== */

static int WriteAll(
    int fd,
    const char *text)
{
    size_t length;
    size_t written;


    length =
        strlen(text);


    written =
        0U;


    while (written <
           length)
    {
        ssize_t result;


        result =
            write(
                fd,
                text + written,
                length - written
            );


        if (result < 0)
        {
            if (errno ==
                EINTR)
            {
                continue;
            }


            perror(
                "UART write"
            );


            return -1;
        }


        written +=
            (size_t)result;
    }


    if (tcdrain(
            fd) != 0)
    {
        perror(
            "tcdrain"
        );


        return -1;
    }


    return 0;
}


/* ============================================================================
 * UART READ WITH TIMEOUT
 * ========================================================================== */

static int ReadLineTimeout(
    int fd,
    char *buffer,
    size_t bufferSize,
    int timeoutSeconds)
{
    size_t index =
        0U;


    while (index <
           (bufferSize - 1U))
    {
        fd_set readSet;

        struct timeval timeout;

        int result;

        char receivedCharacter;


        FD_ZERO(
            &readSet
        );


        FD_SET(
            fd,
            &readSet
        );


        timeout.tv_sec =
            timeoutSeconds;


        timeout.tv_usec =
            0;


        result =
            select(
                fd + 1,
                &readSet,
                NULL,
                NULL,
                &timeout
            );


        if (result < 0)
        {
            if (errno ==
                EINTR)
            {
                continue;
            }


            return -1;
        }


        if (result == 0)
        {
            return 0;
        }


        if (read(
                fd,
                &receivedCharacter,
                1) == 1)
        {
            if (receivedCharacter ==
                '\r')
            {
                continue;
            }


            if (receivedCharacter ==
                '\n')
            {
                if (index ==
                    0U)
                {
                    continue;
                }


                buffer[index] =
                    '\0';


                return 1;
            }


            buffer[index++] =
                receivedCharacter;
        }
    }


    buffer[index] =
        '\0';


    return 1;
}


/* ============================================================================
 * SEND COMMAND TO TM4C
 * ========================================================================== */

static int SendTm4cCommand(
    int fd,
    const char *command,
    const char *expectedResponse,
    int timeoutSeconds)
{
    char response[
        RESPONSE_SIZE
    ];


    /*
     * Remove stale receive data before transmitting
     * a new command.
     */

    (void)tcflush(
        fd,
        TCIFLUSH
    );


    printf(
        "Pi -> TM4C : %s",
        command
    );


    if (WriteAll(
            fd,
            command) != 0)
    {
        uartOnline =
            false;


        RefreshHud();


        return -1;
    }


    while (1)
    {
        int result;


        result =
            ReadLineTimeout(
                fd,
                response,
                sizeof(response),
                timeoutSeconds
            );


        if (result <= 0)
        {
            fprintf(
                stderr,
                "UART response timeout.\n"
            );


            uartOnline =
                false;


            RefreshHud();


            return -1;
        }


        printf(
            "TM4C -> Pi : %s\n",
            response
        );


        if (strcmp(
                response,
                expectedResponse) == 0)
        {
            uartOnline =
                true;


            return 0;
        }


        if ((strcmp(
                response,
                "ERROR") == 0) ||
            (strcmp(
                response,
                "RANGE") == 0) ||
            (strcmp(
                response,
                "UNKNOWN") == 0))
        {
            uartOnline =
                false;


            RefreshHud();


            return -1;
        }
    }
}


/* ============================================================================
 * PARSE SERVO ANGLE
 * ========================================================================== */

static bool ParseAngle(
    const char *text,
    int minimum,
    int maximum,
    int *angle)
{
    char *endPointer;

    long value;


    errno =
        0;


    value =
        strtol(
            text,
            &endPointer,
            10
        );


    while ((*endPointer == ' ') ||
           (*endPointer == '\t'))
    {
        endPointer++;
    }


    if ((errno != 0) ||
        (*text == '\0') ||
        (*endPointer != '\0'))
    {
        return false;
    }


    if ((value < minimum) ||
        (value > maximum))
    {
        return false;
    }


    *angle =
        (int)value;


    return true;
}


/* ============================================================================
 * COMMAND MENU
 * ========================================================================== */

static void PrintMenu(void)
{
    printf(
        "\n"
        "EO Platform HUD Commands\n"
        "------------------------\n"
        "ping\n"
        "center\n"
        "pan <45-135>\n"
        "tilt <55-125>\n"
        "status\n"
        "quit\n"
        "\n"
    );
}


/* ============================================================================
 * MAIN
 * ========================================================================== */

int main(void)
{
    int serialFd;

    char input[
        INPUT_SIZE
    ];


    printf(
        "\n"
        "============================================\n"
        " Real-Time Electro-Optical Platform\n"
        " TM4C123 + Camera + OLED HUD\n"
        "============================================\n\n"
    );


    /* ---------------------------------------------------------------------
     * OLED INITIALIZATION
     * ------------------------------------------------------------------ */

    if (OledInitialize() != 0)
    {
        fprintf(
            stderr,
            "ERROR: OLED initialization failed.\n"
        );


        return EXIT_FAILURE;
    }


    /*
     * Initial screen before UART verification.
     */

    RefreshHud();


    /* ---------------------------------------------------------------------
     * UART OPEN
     * ------------------------------------------------------------------ */

    serialFd =
        open(
            SERIAL_DEVICE,
            O_RDWR |
            O_NOCTTY
        );


    if (serialFd < 0)
    {
        perror(
            "Unable to open UART"
        );


        close(
            oledFd
        );


        return EXIT_FAILURE;
    }


    if (ConfigureSerialPort(
            serialFd) != 0)
    {
        close(
            serialFd
        );


        close(
            oledFd
        );


        return EXIT_FAILURE;
    }


    /* ---------------------------------------------------------------------
     * STARTUP UART VERIFICATION
     *
     * The first UART request occasionally arrives before the serial
     * interface has fully settled.
     *
     * Up to three PING attempts are therefore allowed.
     * ------------------------------------------------------------------ */

    {
        int attempt;


        sleep(1);


        uartOnline =
            false;


        for (attempt = 1;
             attempt <= 3;
             attempt++)
        {
            printf(
                "UART startup check: %d/3\n",
                attempt
            );


            if (SendTm4cCommand(
                    serialFd,
                    "PING\n",
                    "ACK",
                    2) == 0)
            {
                printf(
                    "UART link established.\n"
                );


                break;
            }


            if (attempt < 3)
            {
                printf(
                    "Retrying UART link...\n"
                );


                sleep(1);
            }
        }
    }


    /* ---------------------------------------------------------------------
     * INITIAL CENTER POSITION
     * ------------------------------------------------------------------ */

    if (SendTm4cCommand(
            serialFd,
            "CENTER\n",
            "CENTER_OK",
            2) == 0)
    {
        currentPan =
            CENTER_ANGLE;


        currentTilt =
            CENTER_ANGLE;
    }


    RefreshHud();


    PrintMenu();


    /* =========================================================================
     * MAIN INTERACTIVE CONTROL LOOP
     * ====================================================================== */

    while (1)
    {
        int inputReady = 0;

        printf(
            "EO> "
        );

        fflush(
            stdout
        );

        /*
         * Wait for a terminal command, but do not block the HUD.
         * Every 250 ms the OLED is refreshed so external target
         * state changes appear automatically.
         */
        while (!inputReady)
        {
            fd_set readSet;
            struct timeval timeout;
            int selectResult;

            FD_ZERO(
                &readSet
            );

            FD_SET(
                STDIN_FILENO,
                &readSet
            );

            timeout.tv_sec = 0;
            timeout.tv_usec = 250000;

            selectResult = select(
                STDIN_FILENO + 1,
                &readSet,
                NULL,
                NULL,
                &timeout
            );

            if (selectResult < 0)
            {
                perror(
                    "select"
                );

                continue;
            }

            if (selectResult == 0)
            {
                RefreshHud();
                continue;
            }

            inputReady = 1;
        }

        if (fgets(
                input,
                sizeof(input),
                stdin) == NULL)
        {
            break;
        }


        input[
            strcspn(
                input,
                "\r\n"
            )
        ] =
            '\0';


        /* -----------------------------------------------------------------
         * PING
         * -------------------------------------------------------------- */

        if (strcmp(
                input,
                "ping") == 0)
        {
            (void)SendTm4cCommand(
                serialFd,
                "PING\n",
                "ACK",
                2
            );


            RefreshHud();
        }


        /* -----------------------------------------------------------------
         * CENTER
         * -------------------------------------------------------------- */

        else if (strcmp(
                     input,
                     "center") == 0)
        {
            if (SendTm4cCommand(
                    serialFd,
                    "CENTER\n",
                    "CENTER_OK",
                    2) == 0)
            {
                currentPan =
                    CENTER_ANGLE;


                currentTilt =
                    CENTER_ANGLE;
            }


            RefreshHud();
        }


        /* -----------------------------------------------------------------
         * PAN
         * -------------------------------------------------------------- */

        else if (strncmp(
                     input,
                     "pan ",
                     4U) == 0)
        {
            int angle;

            char command[
                COMMAND_SIZE
            ];


            if (!ParseAngle(
                    input + 4,
                    PAN_MIN_ANGLE,
                    PAN_MAX_ANGLE,
                    &angle))
            {
                printf(
                    "PAN range: %d-%d degrees\n",
                    PAN_MIN_ANGLE,
                    PAN_MAX_ANGLE
                );


                continue;
            }


            snprintf(
                command,
                sizeof(command),
                "PAN %d\n",
                angle
            );


            if (SendTm4cCommand(
                    serialFd,
                    command,
                    "PAN_OK",
                    2) == 0)
            {
                currentPan =
                    angle;
            }


            RefreshHud();
        }


        /* -----------------------------------------------------------------
         * TILT
         * -------------------------------------------------------------- */

        else if (strncmp(
                     input,
                     "tilt ",
                     5U) == 0)
        {
            int angle;

            char command[
                COMMAND_SIZE
            ];


            if (!ParseAngle(
                    input + 5,
                    TILT_MIN_ANGLE,
                    TILT_MAX_ANGLE,
                    &angle))
            {
                printf(
                    "TILT range: %d-%d degrees\n",
                    TILT_MIN_ANGLE,
                    TILT_MAX_ANGLE
                );


                continue;
            }


            snprintf(
                command,
                sizeof(command),
                "TILT %d\n",
                angle
            );


            if (SendTm4cCommand(
                    serialFd,
                    command,
                    "TILT_OK",
                    2) == 0)
            {
                currentTilt =
                    angle;
            }


            RefreshHud();
        }


        /* -----------------------------------------------------------------
         * STATUS
         * -------------------------------------------------------------- */

        else if (strcmp(
                     input,
                     "status") == 0)
        {
            printf(
                "\n"
                "Platform Status\n"
                "---------------\n"
                "Camera : %s\n"
                "PAN/AZ : %d degrees\n"
                "TILT/EL: %d degrees\n"
                "UART   : %s\n"
                "\n",
                CameraConnected()
                    ? "READY"
                    : "OFFLINE",
                currentPan,
                currentTilt,
                uartOnline
                    ? "OK"
                    : "ERROR"
            );


            RefreshHud();
        }


        /* -----------------------------------------------------------------
         * QUIT
         * -------------------------------------------------------------- */

        else if ((strcmp(
                      input,
                      "quit") == 0) ||
                 (strcmp(
                      input,
                      "q") == 0))
        {
            break;
        }


        /* -----------------------------------------------------------------
         * UNKNOWN COMMAND
         * -------------------------------------------------------------- */

        else if (input[0] != '\0')
        {
            PrintMenu();
        }
    }


    /* =========================================================================
     * CLEAN SHUTDOWN
     * ====================================================================== */

    printf(
        "\nReturning platform to center...\n"
    );


    if (SendTm4cCommand(
            serialFd,
            "CENTER\n",
            "CENTER_OK",
            2) == 0)
    {
        currentPan =
            CENTER_ANGLE;


        currentTilt =
            CENTER_ANGLE;
    }


    RefreshHud();


    close(
        serialFd
    );


    close(
        oledFd
    );


    printf(
        "EO platform controller stopped cleanly.\n"
    );


    return EXIT_SUCCESS;
}