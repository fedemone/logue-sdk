#include <cstdio>

// Add these debug helpers to ripplerx.h Render() function

// === DEBUG VERSION: Temporary instrumentation ===

inline void Render_DEBUG(float * __restrict outBuffer, size_t frames)
{
    // Load parameters once per render call
    const bool a_on = (bool)getParameterValue(programParameters::a_on);
    const bool b_on = (bool)getParameterValue(programParameters::b_on);
    const float32_t gain = (float32_t)getParameterValue(programParameters::gain);

    static int frameCount = 0;
    static int renderCount = 0;
    const bool shouldLog = (renderCount++ % 48) == 0;  // Log ~every 100ms at 48kHz

    if (shouldLog) {
        // the only screen that drumlogue have is the one with
        // the paramters set in header.c
        // For debugging, so uncomment the debug lines in constants.h
        // and set the debug program
        // setCurrentProgram[last_program-1]
        // print in user editable programParameters{} the relevant values
        // we would like to inspect (in the following case, using printf)
        //
        // printf("\n=== RENDER CALL %d (frame %d) ===\n", renderCount, frameCount);
        // printf("Frames to process: %zu\n", frames);
        // printf("Output buffer ptr: %p\n", (void*)outBuffer);
        // printf("Sample state: ptr=%p, index=%zu, end=%zu, channels=%u\n",
        //        (void*)m_samplePointer, m_sampleIndex, m_sampleEnd, m_sampleChannels);
        // printf("Resonators: a_on=%d, b_on=%d\n", a_on, b_on);
        // printf("Gain: %.6f\n", gain);

        // Check voices
        int activeVoices = 0;
        for (size_t i = 0; i < c_numVoices; ++i) {
            if (voices[i].m_initialized && voices[i].m_gate) {
                activeVoices++;
                printf("  Voice %zu: initialized=%d, gate=%d, isPressed=%d, frames_since=%zu\n",
                       i, voices[i].m_initialized, voices[i].m_gate,
                       voices[i].isPressed, voices[i].m_framesSinceNoteOn);
            }
        }
        printf("Active voices: %d\n", activeVoices);
    }

    // Load precompute constants
    const float32x4_t v_zero = vdupq_n_f32(0.0f);
    const float32x4_t v_ab_mix = vdupq_n_f32((float32_t)getParameterValue(programParameters::ab_mix));
    const float32x4_t v_one_minus_ab_mix = vdupq_n_f32(1.0f - (float32_t)getParameterValue(programParameters::ab_mix));

    // Main frame loop
    float32x4_t maxSample = vdupq_n_f32(0.0f);  // Track max output for monitoring

    for (size_t frame = 0; frame < frames/2; frame++) {
        float32x4_t dirOut = vdupq_n_f32(0.0f);
        float32x4_t resAOut = vdupq_n_f32(0.0f);
        float32x4_t resBOut = vdupq_n_f32(0.0f);
        float32x4_t audioIn = vdupq_n_f32(0.0f);

        // Load stereo sample input with strict bounds checking
        if (m_samplePointer != nullptr && m_sampleIndex < m_sampleEnd) {
            if (m_sampleChannels == 2) {
                if (m_sampleIndex + 4 <= m_sampleEnd) {
                    audioIn = vld1q_f32(&m_samplePointer[m_sampleIndex]);
                    m_sampleIndex += 4;
                } else {
                    audioIn = vdupq_n_f32(0.0f);
                    m_sampleIndex = m_sampleEnd;
                }
            } else {
                // Mono case
                if (m_sampleIndex + 2 <= m_sampleEnd) {
                    float32_t m1 = m_samplePointer[m_sampleIndex];
                    float32_t m2 = m_samplePointer[m_sampleIndex + 1];
                    float32x2_t s1 = vdup_n_f32(m1);
                    float32x2_t s2 = vdup_n_f32(m2);
                    audioIn = vcombine_f32(s1, s2);
                    m_sampleIndex += 2;
                } else if (m_sampleIndex + 1 <= m_sampleEnd) {
                    float32_t m1 = m_samplePointer[m_sampleIndex];
                    float32x2_t s1 = vdup_n_f32(m1);
                    float32x2_t s2 = vdup_n_f32(0.0f);
                    audioIn = vcombine_f32(s1, s2);
                    m_sampleIndex = m_sampleEnd;
                } else {
                    audioIn = vdupq_n_f32(0.0f);
                }
            }
            audioIn = vmulq_n_f32(audioIn, gain);
        }

        // Process voices (existing code)
        for (size_t i = 0; i < c_numVoices; ++i) {
            Voice& voice = voices[i];

            if (!voice.m_initialized) continue;

            float32x4_t resOut = vdupq_n_f32(0.0f);

            // Mallet processing
            float32_t msample = voice.mallet.process();
            if (msample != 0.0f) {
                float32_t mallet_mix_vel = fmax(0.0f, fmin(1.0f,
                    (float32_t)getParameterValue(programParameters::mallet_mix) +
                    voice.vel * (float32_t)getParameterValue(programParameters::vel_mallet_mix)));
                dirOut = vmlaq_n_f32(dirOut, vdupq_n_f32(msample), mallet_mix_vel);
            }

            // Add input sample only if voice is pressed
            if (voice.isPressed) {
                resOut = vaddq_f32(resOut, audioIn);
            }

            // Noise processing
            float32_t nsample = voice.noise.process();
            if (nsample != 0.0f) {
                float32_t noise_mix_vel = fmax(0.0f, fmin(1.0f,
                    (float32_t)getParameterValue(programParameters::noise_mix) +
                    voice.vel * (float32_t)getParameterValue(programParameters::vel_noise_mix)));
                dirOut = vmlaq_n_f32(dirOut, vdupq_n_f32(nsample), noise_mix_vel);
            }

            // Resonator processing (simplified for debug)
            if (a_on) {
                float32x4_t out = voice.resA.process(resOut);
                resAOut = vaddq_f32(resAOut, out);
            }
            if (b_on) {
                float32x4_t resB_input = resOut;
                float32x4_t out = voice.resB.process(resB_input);
                resBOut = vaddq_f32(resBOut, out);
            }

            voice.m_framesSinceNoteOn += 2;
        }

        // Mix and output
        float32x4_t resOut = vaddq_f32(resAOut, resBOut);
        float32x4_t totalOut = vmlaq_n_f32(dirOut, resOut, gain);
        float32x4_t split = comb.process(totalOut);
        float32x4_t channels = limiter.process(split);

        // Accumulate into output buffer
        float32x4_t old = vld1q_f32(outBuffer);
        channels = vaddq_f32(old, channels);
        vst1q_f32(outBuffer, channels);

        // Track max for monitoring
        float32x4_t absSamples = vabsq_f32(channels);
        maxSample = vmaxq_f32(maxSample, absSamples);

        outBuffer += 4;
    }

    if (shouldLog) {
        // Report max output level
        float maxArray[4];
        vst1q_f32(maxArray, maxSample);
        float maxVal = fmax(fmax(maxArray[0], maxArray[1]),
                           fmax(maxArray[2], maxArray[3]));
        printf("Max output level this render: %.6f\n", maxVal);
        if (maxVal > 2.0f) printf("  WARNING: Clipping detected (>2.0)\n");
        if (maxVal < 0.0001f) printf("  WARNING: Very low output (<0.0001)\n");
        printf("=== END RENDER ===\n");
    }
}

// === SIMPLE VERIFICATION FUNCTION ===

void verifyAudioPath() {
    printf("\n=== AUDIO PATH VERIFICATION ===\n");

    // Check 1: Resonator enable
    bool a_on = (bool)getParameterValue(programParameters::a_on);
    bool b_on = (bool)getParameterValue(programParameters::b_on);
    printf("Resonator A enabled: %s\n", a_on ? "YES" : "NO");
    printf("Resonator B enabled: %s\n", b_on ? "YES" : "NO");
    if (!a_on && !b_on) {
        printf("  ERROR: Both resonators disabled! No sound possible.\n");
    }

    // Check 2: Gain
    float gain = (float)getParameterValue(programParameters::gain);
    printf("Gain value: %.6f\n", gain);
    if (gain < 0.0001f) {
        printf("  ERROR: Gain is effectively zero!\n");
    }
    if (gain > 10.0f) {
        printf("  WARNING: Gain is very high (may cause clipping)\n");
    }

    // Check 3: Voice state
    printf("Voice states:\n");
    int initializedCount = 0;
    for (size_t i = 0; i < c_numVoices; ++i) {
        if (voices[i].m_initialized) {
            initializedCount++;
            printf("  Voice %zu: initialized, gate=%d, note=%d\n",
                   i, voices[i].m_gate, voices[i].note);
        }
    }
    printf("Total initialized voices: %d\n", initializedCount);

    // Check 4: Sample access
    printf("Sample resources:\n");
    printf("  get_num_sample_banks: %p\n", (void*)m_get_num_sample_banks_ptr);
    printf("  get_sample: %p\n", (void*)m_get_sample);
    if (m_get_num_sample_banks_ptr && m_get_sample) {
        printf("  Sample banks available: %u\n", m_get_num_sample_banks_ptr());
    }

    // Check 5: Mallet/Noise
    printf("Mallet parameters:\n");
    printf("  Mallet mix: %.3f\n", getParameterValue(programParameters::mallet_mix));
    printf("  Noise mix: %.3f\n", getParameterValue(programParameters::noise_mix));

    printf("=== END VERIFICATION ===\n\n");
}
