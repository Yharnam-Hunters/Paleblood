// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for FMOD output-descriptor callbacks of the Orbis audio
// output. Every output pointer is optional.

enum {
    DRIVER_COUNT = 1,
    DEFAULT_FORMAT_VALUE = 0x84,
    DEFAULT_SAMPLE_RATE = 48000,
    DEFAULT_SPEAKER_VALUE = 6,
};

// Reports one output driver.
int audio_fmod_output_driver_count(void *output_state, int *driver_count)
{
    (void)output_state;
    if (driver_count)
        *driver_count = DRIVER_COUNT;
    return 0;
}

// Reports the default output settings: 132, 48000 Hz and 6 (meanings of 132 and 6 unconfirmed).
int audio_fmod_output_defaults(void *output_state, int driver, int *format, int *sample_rate,
                               int *speaker_value)
{
    (void)output_state;
    (void)driver;
    if (format)
        *format = DEFAULT_FORMAT_VALUE;
    if (sample_rate)
        *sample_rate = DEFAULT_SAMPLE_RATE;
    if (speaker_value)
        *speaker_value = DEFAULT_SPEAKER_VALUE;
    return 0;
}
