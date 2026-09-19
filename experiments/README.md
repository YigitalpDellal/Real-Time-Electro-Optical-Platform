# Visual Tracking Experiments

During development, classical OpenCV trackers including MOSSE, KCF, and CSRT were evaluated as possible automatic visual-lock mechanisms.

The experiments were useful for understanding the trade-offs between tracking quality, reacquisition, target drift, and Raspberry Pi 3 compute limits. They were not included in the stable release because the result was less predictable than the final scan + target-event design.

The stable project therefore uses:

- deterministic manual PAN/TILT control,
- automatic horizontal scanning,
- motion-based target acquisition/loss events,
- automatic scan stop on target detection.

Experimental source files are intentionally not mixed with the stable release unless they are added later as a separate documented branch or folder.