/*
 * ============================================================================
 * uart_pan_tilt_control.c
 * ============================================================================
 *
 * Real-Time Electro-Optical Monitoring Platform
 *
 * TM4C123 UART Controlled Dual Servo Pan-Tilt Firmware
 *
 * --------------------------------------------------------------------------
 * SYSTEM ARCHITECTURE
 * --------------------------------------------------------------------------
 *
 * Raspberry Pi 3B+
 *       |
 *       | UART 115200 8-N-1
 *       |
 *       v
 * +----------------------+
 * |       TM4C123        |
 * |                      |
 * | UART1 Command Parser |
 * | PWM Generator        |
 * +----------+-----------+
 *            |
 *        +---+---+
 *        |       |
 *       PAN     TILT
 *      Servo    Servo
 *
 *
 * --------------------------------------------------------------------------
 * HARDWARE CONNECTIONS
 * --------------------------------------------------------------------------
 *
 * PAN servo signal:
 *
 *      TM4C123 PB6
 *      M0PWM0
 *
 * TILT servo signal:
 *
 *      TM4C123 PB7
 *      M0PWM1
 *
 *
 * Raspberry Pi UART:
 *
 *      Raspberry Pi TX  --->  PB0 / U1RX
 *      Raspberry Pi RX  <---  PB1 / U1TX
 *
 * IMPORTANT:
 *
 *      Raspberry Pi GND
 *      TM4C123 GND
 *      Servo power supply GND
 *
 * must share a common ground.
 *
 *
 * --------------------------------------------------------------------------
 * UART CONFIGURATION
 * --------------------------------------------------------------------------
 *
 * Baud rate : 115200
 * Data bits : 8
 * Parity    : None
 * Stop bits : 1
 *
 *
 * --------------------------------------------------------------------------
 * SUPPORTED COMMANDS
 * --------------------------------------------------------------------------
 *
 *      PING
 *
 *          Response:
 *              ACK
 *
 *
 *      CENTER
 *
 *          Moves both servos to 90 degrees.
 *
 *          Response:
 *              CENTER_OK
 *
 *
 *      PAN <angle>
 *
 *          Allowed range:
 *              45 - 135 degrees
 *
 *          Response:
 *              PAN_OK
 *
 *
 *      TILT <angle>
 *
 *          Allowed range:
 *              55 - 125 degrees
 *
 *          Response:
 *              TILT_OK
 *
 *
 *      TEST
 *
 *          Performs a full pan-tilt movement test.
 *
 *          Responses:
 *              TEST_START
 *              TEST_OK
 *
 *
 * Invalid angle:
 *
 *      RANGE
 *
 *
 * Invalid numeric format:
 *
 *      ERROR
 *
 *
 * Unknown command:
 *
 *      UNKNOWN
 *
 *
 * --------------------------------------------------------------------------
 * SERVO CALIBRATION
 * --------------------------------------------------------------------------
 *
 * The servo PWM frequency is 50 Hz.
 *
 * PWM period:
 *
 *      20 ms
 *
 *
 * Pulse calibration used by this firmware:
 *
 *      0 degrees   -> approximately 500 us
 *      90 degrees  -> approximately 1500 us
 *      180 degrees -> approximately 2500 us
 *
 *
 * IMPORTANT:
 *
 * The program DOES NOT allow the full 0-180 degree command range.
 *
 * Software limits are:
 *
 *      PAN  : 45 - 135 degrees
 *      TILT : 55 - 125 degrees
 *
 * Therefore the normal operating pulses remain considerably inside
 * the extreme calibration limits.
 *
 * Example PAN pulses:
 *
 *      PAN 45  -> approximately 1000 us
 *      PAN 90  -> approximately 1500 us
 *      PAN 135 -> approximately 2000 us
 *
 * This gives a noticeably larger mechanical movement than the previous
 * 1000-2000 us calibration while retaining software safety limits.
 *
 * ============================================================================
 */


#include <stdint.h>
#include <stdbool.h>
#include <string.h>


/*
 * TM4C123 / TivaWare hardware definitions.
 */
#include "inc/hw_memmap.h"


/*
 * TivaWare DriverLib.
 */
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/pwm.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"


/* ============================================================================
 * SYSTEM CLOCK CONFIGURATION
 * ========================================================================== */

/*
 * TM4C123 system clock:
 *
 *      80 MHz
 */
#define SYSTEM_CLOCK_HZ        80000000UL


/* ============================================================================
 * PWM CONFIGURATION
 * ========================================================================== */

/*
 * PWM peripheral clock divider.
 *
 * System clock:
 *
 *      80 MHz
 *
 * PWM divider:
 *
 *      64
 *
 * Therefore:
 *
 *      PWM clock = 80 MHz / 64
 *                = 1.25 MHz
 */
#define PWM_DIVIDER            64UL


#define PWM_CLOCK_HZ           \
    (SYSTEM_CLOCK_HZ / PWM_DIVIDER)


/*
 * Standard hobby servo frequency.
 */
#define SERVO_FREQUENCY_HZ     50UL


/*
 * Number of PWM timer counts in one complete 20 ms period.
 *
 *      1.25 MHz / 50 Hz
 *
 *      = 25,000 counts
 */
#define PWM_PERIOD_COUNTS      \
    (PWM_CLOCK_HZ / SERVO_FREQUENCY_HZ)


/* ============================================================================
 * SERVO CALIBRATION
 * ========================================================================== */

/*
 * Wider servo calibration.
 *
 * Previous version:
 *
 *      1000 us - 2000 us
 *
 * New version:
 *
 *      500 us - 2500 us
 *
 * The actual commanded angle ranges remain limited in software.
 */
#define SERVO_MIN_PULSE_US     500UL
#define SERVO_MAX_PULSE_US     2500UL


/*
 * Neutral mechanical position.
 */
#define CENTER_ANGLE           90U


/* ============================================================================
 * SAFE PAN OPERATING RANGE
 * ========================================================================== */

/*
 * Horizontal camera movement limits.
 */
#define PAN_MIN_ANGLE          45U
#define PAN_MAX_ANGLE          135U


/* ============================================================================
 * SAFE TILT OPERATING RANGE
 * ========================================================================== */

/*
 * Vertical camera movement limits.
 */
#define TILT_MIN_ANGLE         55U
#define TILT_MAX_ANGLE         125U


/* ============================================================================
 * UART CONFIGURATION
 * ========================================================================== */

#define UART_BAUD_RATE         115200UL


/*
 * Maximum received command length.
 *
 * Examples:
 *
 *      "PING"
 *      "CENTER"
 *      "PAN 135"
 *      "TILT 125"
 */
#define RX_BUFFER_SIZE         32U


/* ============================================================================
 * SERVO TEST CONFIGURATION
 * ========================================================================== */

/*
 * Delay between TEST positions.
 *
 * 350 ms gives the servo enough time to travel across the wider
 * mechanical range before another target position is commanded.
 */
#define TEST_DELAY_MS          350U


/* ============================================================================
 * DELAY FUNCTION
 * ========================================================================== */

/*
 * Simple millisecond delay.
 *
 * SysCtlDelay() consumes approximately three CPU cycles per loop.
 *
 * This function is used only for servo settling and TEST movement.
 * PWM itself is generated completely by hardware.
 */
static void DelayMs(
    uint32_t milliseconds)
{
    SysCtlDelay(
        (SYSTEM_CLOCK_HZ / 3000UL) *
        milliseconds
    );
}


/* ============================================================================
 * SERVO PULSE CONVERSION
 * ========================================================================== */

/*
 * Convert pulse duration in microseconds into PWM timer counts.
 *
 * Example:
 *
 * PWM clock = 1.25 MHz
 *
 * 1500 us pulse:
 *
 *      1,250,000 * 1500 / 1,000,000
 *
 *      = 1875 counts
 */
static uint32_t ServoPulseUsToCounts(
    uint32_t pulseWidthUs)
{
    return
        (PWM_CLOCK_HZ *
         pulseWidthUs)
        / 1000000UL;
}


/*
 * Convert an angle from 0-180 degrees into a servo PWM pulse.
 *
 * Calibration:
 *
 *      0 degrees   -> 500 us
 *      90 degrees  -> 1500 us
 *      180 degrees -> 2500 us
 *
 * Linear interpolation is used between these values.
 */
static uint32_t ServoAngleToCounts(
    uint32_t angle)
{
    uint32_t pulseWidthUs;


    /*
     * Defensive protection.
     *
     * Command processing already enforces narrower PAN/TILT limits,
     * but this protects the conversion routine independently.
     */
    if (angle > 180U)
    {
        angle =
            180U;
    }


    /*
     * Linear mapping:
     *
     * pulse =
     *      minimum +
     *      angle * pulse_range / 180
     */
    pulseWidthUs =
        SERVO_MIN_PULSE_US +
        (
            angle *
            (
                SERVO_MAX_PULSE_US -
                SERVO_MIN_PULSE_US
            )
        ) / 180U;


    return
        ServoPulseUsToCounts(
            pulseWidthUs
        );
}


/* ============================================================================
 * PAN SERVO CONTROL
 * ========================================================================== */

/*
 * Update PAN servo PWM duty cycle.
 *
 * PAN is connected to:
 *
 *      PB6
 *      M0PWM0
 *      PWM_OUT_0
 */
static void SetPanAngle(
    uint32_t angle)
{
    PWMPulseWidthSet(
        PWM0_BASE,
        PWM_OUT_0,
        ServoAngleToCounts(
            angle
        )
    );
}


/* ============================================================================
 * TILT SERVO CONTROL
 * ========================================================================== */

/*
 * Update TILT servo PWM duty cycle.
 *
 * TILT is connected to:
 *
 *      PB7
 *      M0PWM1
 *      PWM_OUT_1
 */
static void SetTiltAngle(
    uint32_t angle)
{
    PWMPulseWidthSet(
        PWM0_BASE,
        PWM_OUT_1,
        ServoAngleToCounts(
            angle
        )
    );
}


/* ============================================================================
 * SYSTEM CLOCK INITIALIZATION
 * ========================================================================== */

/*
 * Configure the TM4C123 system clock.
 *
 * Hardware:
 *
 *      16 MHz external crystal
 *
 * PLL:
 *
 *      enabled
 *
 * Final system clock:
 *
 *      80 MHz
 */
static void ConfigureSystemClock(void)
{
    SysCtlClockSet(
        SYSCTL_SYSDIV_2_5 |
        SYSCTL_USE_PLL |
        SYSCTL_OSC_MAIN |
        SYSCTL_XTAL_16MHZ
    );
}


/* ============================================================================
 * PWM INITIALIZATION
 * ========================================================================== */

/*
 * Configure PWM Module 0 Generator 0.
 *
 * Generator 0 produces:
 *
 *      PWM output 0 -> PB6 -> PAN
 *      PWM output 1 -> PB7 -> TILT
 *
 * Both outputs therefore share the same 50 Hz PWM period but have
 * independent pulse widths.
 */
static void ConfigureServoPWM(void)
{
    /*
     * Enable GPIO Port B.
     *
     * Port B is used by both:
     *
     *      PWM
     *      UART1
     */
    SysCtlPeripheralEnable(
        SYSCTL_PERIPH_GPIOB
    );


    /*
     * Enable PWM Module 0.
     */
    SysCtlPeripheralEnable(
        SYSCTL_PERIPH_PWM0
    );


    /*
     * Wait until peripherals are ready.
     */
    while (!SysCtlPeripheralReady(
        SYSCTL_PERIPH_GPIOB))
    {
    }


    while (!SysCtlPeripheralReady(
        SYSCTL_PERIPH_PWM0))
    {
    }


    /*
     * PWM clock:
     *
     *      80 MHz / 64
     *
     *      = 1.25 MHz
     */
    SysCtlPWMClockSet(
        SYSCTL_PWMDIV_64
    );


    /*
     * Configure PB6 for PWM output 0.
     *
     * PAN servo.
     */
    GPIOPinConfigure(
        GPIO_PB6_M0PWM0
    );


    /*
     * Configure PB7 for PWM output 1.
     *
     * TILT servo.
     */
    GPIOPinConfigure(
        GPIO_PB7_M0PWM1
    );


    /*
     * Enable PWM functionality on PB6 and PB7.
     */
    GPIOPinTypePWM(
        GPIO_PORTB_BASE,
        GPIO_PIN_6 |
        GPIO_PIN_7
    );


    /*
     * Configure PWM Generator 0.
     *
     * Down-count mode is sufficient for standard servo PWM.
     */
    PWMGenConfigure(
        PWM0_BASE,
        PWM_GEN_0,
        PWM_GEN_MODE_DOWN |
        PWM_GEN_MODE_NO_SYNC
    );


    /*
     * Set generator period for 50 Hz.
     *
     * Period:
     *
     *      20 ms
     */
    PWMGenPeriodSet(
        PWM0_BASE,
        PWM_GEN_0,
        PWM_PERIOD_COUNTS
    );


    /*
     * Start both servos at the neutral 90 degree position.
     */
    SetPanAngle(
        CENTER_ANGLE
    );


    SetTiltAngle(
        CENTER_ANGLE
    );


    /*
     * Enable PAN and TILT PWM outputs.
     */
    PWMOutputState(
        PWM0_BASE,
        PWM_OUT_0_BIT |
        PWM_OUT_1_BIT,
        true
    );


    /*
     * Start PWM Generator 0.
     */
    PWMGenEnable(
        PWM0_BASE,
        PWM_GEN_0
    );


    /*
     * Allow both servos time to physically settle at center.
     */
    DelayMs(
        1000U
    );
}


/* ============================================================================
 * UART1 INITIALIZATION
 * ========================================================================== */

/*
 * UART1 pin assignment:
 *
 *      PB0 -> U1RX
 *      PB1 -> U1TX
 *
 * Communication:
 *
 *      Raspberry Pi TX -> PB0
 *      Raspberry Pi RX <- PB1
 */
static void ConfigureUART1(void)
{
    /*
     * Enable UART1 peripheral.
     */
    SysCtlPeripheralEnable(
        SYSCTL_PERIPH_UART1
    );


    while (!SysCtlPeripheralReady(
        SYSCTL_PERIPH_UART1))
    {
    }


    /*
     * Configure PB0 as UART1 RX.
     */
    GPIOPinConfigure(
        GPIO_PB0_U1RX
    );


    /*
     * Configure PB1 as UART1 TX.
     */
    GPIOPinConfigure(
        GPIO_PB1_U1TX
    );


    /*
     * Enable UART functionality on both pins.
     */
    GPIOPinTypeUART(
        GPIO_PORTB_BASE,
        GPIO_PIN_0 |
        GPIO_PIN_1
    );


    /*
     * UART configuration:
     *
     *      115200 baud
     *      8 data bits
     *      1 stop bit
     *      no parity
     */
    UARTConfigSetExpClk(
        UART1_BASE,
        SYSTEM_CLOCK_HZ,
        UART_BAUD_RATE,
        UART_CONFIG_WLEN_8 |
        UART_CONFIG_STOP_ONE |
        UART_CONFIG_PAR_NONE
    );
}


/* ============================================================================
 * UART TRANSMIT
 * ========================================================================== */

/*
 * Send a null-terminated C string over UART1.
 *
 * Each character is written individually using UARTCharPut().
 */
static void UARTSendString(
    const char *text)
{
    while (*text != '\0')
    {
        UARTCharPut(
            UART1_BASE,
            *text
        );


        text++;
    }
}


/* ============================================================================
 * UNSIGNED INTEGER PARSER
 * ========================================================================== */

/*
 * Convert numeric command text into an unsigned integer.
 *
 * Example:
 *
 *      "135"
 *
 * becomes:
 *
 *      135
 *
 *
 * Returns false if:
 *
 *      - string is empty
 *      - string contains non-numeric characters
 *      - numeric value exceeds 180
 */
static bool ParseUnsigned(
    const char *text,
    uint32_t *value)
{
    uint32_t result =
        0U;


    /*
     * Empty argument is invalid.
     */
    if (*text == '\0')
    {
        return false;
    }


    while (*text != '\0')
    {
        /*
         * Every character must be a decimal digit.
         */
        if ((*text < '0') ||
            (*text > '9'))
        {
            return false;
        }


        result =
            (result * 10U) +
            (uint32_t)(
                *text - '0'
            );


        /*
         * Servo angles larger than 180 degrees are always invalid.
         */
        if (result > 180U)
        {
            return false;
        }


        text++;
    }


    *value =
        result;


    return true;
}


/* ============================================================================
 * SERVO MOVEMENT TEST
 * ========================================================================== */

/*
 * Move both axes through their calibrated operating region.
 *
 * Sequence:
 *
 *      Position 1:
 *
 *          PAN  = 45
 *          TILT = 55
 *
 *
 *      Position 2:
 *
 *          PAN  = 135
 *          TILT = 125
 *
 *
 *      Position 3:
 *
 *          PAN  = 90
 *          TILT = 90
 *
 *
 * TEST_DELAY_MS is inserted between positions so the servos have time
 * to physically reach each target.
 */
static void RunServoTest(void)
{
    /*
     * --------------------------------------------------------
     * POSITION 1
     * --------------------------------------------------------
     */

    SetPanAngle(
        PAN_MIN_ANGLE
    );


    SetTiltAngle(
        TILT_MIN_ANGLE
    );


    DelayMs(
        TEST_DELAY_MS
    );


    /*
     * --------------------------------------------------------
     * POSITION 2
     * --------------------------------------------------------
     */

    SetPanAngle(
        PAN_MAX_ANGLE
    );


    SetTiltAngle(
        TILT_MAX_ANGLE
    );


    DelayMs(
        TEST_DELAY_MS
    );


    /*
     * --------------------------------------------------------
     * RETURN TO CENTER
     * --------------------------------------------------------
     */

    SetPanAngle(
        CENTER_ANGLE
    );


    SetTiltAngle(
        CENTER_ANGLE
    );


    DelayMs(
        TEST_DELAY_MS
    );
}


/* ============================================================================
 * COMMAND PROCESSOR
 * ========================================================================== */

/*
 * Process one complete newline-terminated UART command.
 *
 * This function is responsible for:
 *
 *      command identification
 *      numeric parsing
 *      range checking
 *      servo movement
 *      UART acknowledgement
 */
static void ProcessCommand(
    const char *command)
{
    uint32_t angle;


    /* ========================================================================
     * PING
     * ====================================================================== */

    if (strcmp(
            command,
            "PING") == 0)
    {
        UARTSendString(
            "ACK\n"
        );


        return;
    }


    /* ========================================================================
     * CENTER
     * ====================================================================== */

    if (strcmp(
            command,
            "CENTER") == 0)
    {
        /*
         * Move both axes to the neutral position.
         */
        SetPanAngle(
            CENTER_ANGLE
        );


        SetTiltAngle(
            CENTER_ANGLE
        );


        UARTSendString(
            "CENTER_OK\n"
        );


        return;
    }


    /* ========================================================================
     * TEST
     * ====================================================================== */

    if (strcmp(
            command,
            "TEST") == 0)
    {
        /*
         * Tell Raspberry Pi that physical motion is starting.
         */
        UARTSendString(
            "TEST_START\n"
        );


        RunServoTest();


        /*
         * Tell Raspberry Pi that movement has completed.
         */
        UARTSendString(
            "TEST_OK\n"
        );


        return;
    }


    /* ========================================================================
     * PAN <angle>
     * ====================================================================== */

    if (strncmp(
            command,
            "PAN ",
            4U) == 0)
    {
        /*
         * Parse the numeric characters following "PAN ".
         */
        if (!ParseUnsigned(
                command + 4,
                &angle))
        {
            UARTSendString(
                "ERROR\n"
            );


            return;
        }


        /*
         * Reject mechanically unsafe PAN values.
         */
        if ((angle <
             PAN_MIN_ANGLE) ||
            (angle >
             PAN_MAX_ANGLE))
        {
            UARTSendString(
                "RANGE\n"
            );


            return;
        }


        /*
         * Apply requested PAN angle.
         */
        SetPanAngle(
            angle
        );


        UARTSendString(
            "PAN_OK\n"
        );


        return;
    }


    /* ========================================================================
     * TILT <angle>
     * ====================================================================== */

    if (strncmp(
            command,
            "TILT ",
            5U) == 0)
    {
        /*
         * Parse numeric characters following "TILT ".
         */
        if (!ParseUnsigned(
                command + 5,
                &angle))
        {
            UARTSendString(
                "ERROR\n"
            );


            return;
        }


        /*
         * Reject mechanically unsafe TILT values.
         */
        if ((angle <
             TILT_MIN_ANGLE) ||
            (angle >
             TILT_MAX_ANGLE))
        {
            UARTSendString(
                "RANGE\n"
            );


            return;
        }


        /*
         * Apply requested TILT angle.
         */
        SetTiltAngle(
            angle
        );


        UARTSendString(
            "TILT_OK\n"
        );


        return;
    }


    /* ========================================================================
     * UNKNOWN COMMAND
     * ====================================================================== */

    UARTSendString(
        "UNKNOWN\n"
    );
}


/* ============================================================================
 * MAIN
 * ========================================================================== */

int main(void)
{
    /*
     * UART receive buffer.
     */
    char rxBuffer[
        RX_BUFFER_SIZE
    ];


    /*  
     * Current write position inside receive buffer.
     */
    uint32_t rxIndex =
        0U;


    char receivedCharacter;


    /* ========================================================================
     * SYSTEM INITIALIZATION
     * ====================================================================== */

    /*
     * Configure TM4C system clock to 80 MHz.
     */
    ConfigureSystemClock();


    /*
     * Configure hardware PWM for both servos.
     */
    ConfigureServoPWM();


    /*
     * Configure UART1 communication with Raspberry Pi.
     */
    ConfigureUART1();


    /*
     * Notify Raspberry Pi that firmware initialization has completed.
     */
    UARTSendString(
        "READY\n"
    );


    /* ========================================================================
     * MAIN COMMAND LOOP
     * ====================================================================== */

    while (1)
    {
        /*
         * Wait for one UART character.
         *
         * This is a blocking UART read.
         *
         * Servo PWM continues running independently because PWM generation
         * is handled by the TM4C hardware peripheral rather than software.
         */
        receivedCharacter =
            (char)UARTCharGet(
                UART1_BASE
            );


        /*
         * Newline or carriage return marks the end of one command.
         */
        if ((receivedCharacter ==
             '\n') ||
            (receivedCharacter ==
             '\r'))
        {
            /*
             * Ignore completely empty lines.
             */
            if (rxIndex > 0U)
            {
                /*
                 * Terminate received text as a valid C string.
                 */
                rxBuffer[
                    rxIndex
                ] = '\0';


                /*
                 * Execute received command.
                 */
                ProcessCommand(
                    rxBuffer
                );


                /*
                 * Prepare buffer for next command.
                 */
                rxIndex =
                    0U;
            }
        }

        else
        {
            /*
             * Store character if space remains.
             */
            if (rxIndex <
                (RX_BUFFER_SIZE - 1U))
            {
                rxBuffer[
                    rxIndex
                ] =
                    receivedCharacter;


                rxIndex++;
            }

            else
            {
                /*
                 * Command exceeded receive-buffer capacity.
                 *
                 * Clear the buffer and tell Raspberry Pi that the command
                 * could not be accepted.
                 */
                rxIndex =
                    0U;


                UARTSendString(
                    "ERROR\n"
                );
            }
        }
    }
}