// test_dsp.c - T-041: resampler, EBU R128, true peak, editing, dither, wire
// formats and the whole per-track pipeline.
//
// Every vector is synthesised here in C: no fixture file, no script to run
// first, and the EBU Tech 3341 gating cases are built out of the very sine
// segments the document describes.

#define TEST_DSP_RATE 44100u

// --- small spectral helpers -------------------------------------------------
// Hann-windowed projection onto one frequency. The Hann sidelobes fall off as
// f^-3, so a tone thousands of bins away leaks nothing measurable into the bin
// we are inspecting - which is exactly what an aliasing test needs.
static f64 test_dsp_tone_amp(const f32 *x, u64 count, f64 freq, u32 rate) {
    f64 re = 0.0;
    f64 im = 0.0;
    f64 wsum = 0.0;
    f64 omega = 2.0 * DSP_PI * freq / (f64)rate;
    for (u64 n = 0; n < count; n += 1) {
        f64 w = 0.5 - 0.5 * dsp_cos_f64(2.0 * DSP_PI * (f64)n / (f64)count);
        f64 phase = omega * (f64)n;
        re += w * (f64)x[n] * dsp_cos_f64(phase);
        im -= w * (f64)x[n] * dsp_sin_f64(phase);
        wsum += w;
    }
    f64 magnitude = dsp_sqrt_f64(re * re + im * im);
    return 2.0 * magnitude / wsum;
}

static void test_dsp_fill_sine(f32 *dst, u64 count, f64 freq, u32 rate, f64 amplitude,
                               f64 phase0) {
    f64 omega = 2.0 * DSP_PI * freq / (f64)rate;
    for (u64 n = 0; n < count; n += 1) {
        dst[n] = (f32)(amplitude * dsp_sin_f64(omega * (f64)n + phase0));
    }
}

// --- dsp_math ---------------------------------------------------------------
static b32 test_dsp_close(f64 a, f64 b, f64 tolerance) {
    f64 d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tolerance;
}

TEST(dsp_math) {
    Unused(arena);
    EXPECT(test_dsp_close(dsp_sin_f64(0.0), 0.0, 1e-15));
    EXPECT(test_dsp_close(dsp_sin_f64(DSP_PI / 6.0), 0.5, 1e-14));
    EXPECT(test_dsp_close(dsp_cos_f64(DSP_PI / 3.0), 0.5, 1e-14));
    EXPECT(test_dsp_close(dsp_sin_f64(DSP_PI), 0.0, 1e-14));
    EXPECT(test_dsp_close(dsp_sin_f64(31.7), 0.2802681697690195, 1e-12));
    EXPECT(test_dsp_close(dsp_cos_f64(-97.25), -0.9903033977223353, 1e-12));
    EXPECT(test_dsp_close(dsp_tan_f64(0.4), 0.4227932187381618, 1e-13));

    EXPECT(dsp_exp_f64(0.0) == 1.0);
    EXPECT(test_dsp_close(dsp_exp_f64(1.0), 2.718281828459045, 1e-14));
    EXPECT(test_dsp_close(dsp_exp_f64(-11.5), 1.0130095e-05, 1e-11));
    EXPECT(test_dsp_close(dsp_log_f64(1.0), 0.0, 1e-16));
    EXPECT(test_dsp_close(dsp_log_f64(2.0), DSP_LN2, 1e-15));
    EXPECT(test_dsp_close(dsp_log_f64(1e-7), -16.11809565095832, 1e-12));
    EXPECT(test_dsp_close(dsp_log10_f64(1000.0), 3.0, 1e-13));
    EXPECT(test_dsp_close(dsp_pow_f64(10.0, 0.19999219269866735), 1.5848647011308556, 1e-12));
    EXPECT(test_dsp_close(dsp_bessel_i0(0.0), 1.0, 1e-15));
    EXPECT(test_dsp_close(dsp_bessel_i0(1.0), 1.2660658777520084, 1e-13));
    EXPECT(test_dsp_close(dsp_bessel_i0(9.0), 1093.588354511375, 1e-8));
    EXPECT(test_dsp_close(dsp_sinc(0.0), 1.0, 1e-16));
    EXPECT(test_dsp_close(dsp_sinc(1.0), 0.0, 1e-15));
    EXPECT(test_dsp_close((f64)dsp_db_from_amp(0.5f), -6.020599913279624, 1e-4));
    EXPECT(test_dsp_close((f64)dsp_amp_from_db(-6.020599913279624f), 0.5, 1e-6));
    EXPECT(dsp_amp_from_db(0.0f) == 1.0f);  // the bypass path depends on this
}

// --- resampler --------------------------------------------------------------
TEST(dsp_resample_impulse) {
    DspResampler r;
    // 48000/44100 reduces to 160/147: an impulse at input 160 falls exactly on
    // output 147, phase 0, which is where a latency-compensated resampler must
    // put it and the only place the response can be checked for symmetry.
    dsp_resampler_init(&r, arena, 48000, 44100, 1, 4096, 0);
    EXPECT(r.taps_per_phase == 64);  // 48000/44100 rounds to 1: the base tap count

    u64 in_count = 4096;
    f32 *in = push_array_zero(arena, f32, in_count);
    f32 *out = push_array_zero(arena, f32, 8192);
    in[160] = 1.0f;
    f32 *in_ch[1];
    f32 *out_ch[1];
    in_ch[0] = in;
    out_ch[0] = out;
    u64 produced = dsp_resampler_process(&r, (const f32 *const *)in_ch, in_count, out_ch, 8192);
    EXPECT(produced > 300);

    u64 peak_index = 0;
    f32 peak = 0.0f;
    for (u64 i = 0; i < produced; i += 1) {
        f32 magnitude = (out[i] < 0.0f) ? -out[i] : out[i];
        if (magnitude > peak) {
            peak = magnitude;
            peak_index = i;
        }
    }
    EXPECT(peak_index == 147);

    // Linear phase: the response mirrors around the impulse.
    f64 worst = 0.0;
    for (u64 j = 1; j < 60; j += 1) {
        f64 d = (f64)out[147 - j] - (f64)out[147 + j];
        if (d < 0.0) { d = -d; }
        if (d > worst) { worst = d; }
    }
    EXPECT(worst < 1e-6);
    EXPECT(dsp_resampler_expected_out(48000, 44100, 48000) == 44100);
}

// Common body of the sine tests: resample a tone and compare with the ideal
// tone at the output rate. The first and last taps of the window are edges, so
// the comparison starts and ends clear of them.
static f64 test_dsp_resample_snr(Arena *arena, u32 in_rate, f64 freq, u64 seconds) {
    DspResampler r;
    u64 in_count = (u64)in_rate * seconds;
    dsp_resampler_init(&r, arena, in_rate, TEST_DSP_RATE, 1, 4096, in_count);

    u64 out_cap = dsp_resampler_out_capacity(&r, in_count) + 4096;
    f32 *in = push_array_zero(arena, f32, 4096);
    f32 *out = push_array_zero(arena, f32, out_cap);
    f32 *in_ch[1];
    f32 *out_ch[1];
    in_ch[0] = in;

    u64 produced = 0;
    u64 fed = 0;
    while (fed < in_count) {
        u64 chunk = Min((u64)4096, in_count - fed);
        f64 omega = 2.0 * DSP_PI * freq / (f64)in_rate;
        for (u64 i = 0; i < chunk; i += 1) { in[i] = (f32)dsp_sin_f64(omega * (f64)(fed + i)); }
        out_ch[0] = out + produced;
        produced += dsp_resampler_process(&r, (const f32 *const *)in_ch, chunk, out_ch,
                                          out_cap - produced);
        fed += chunk;
    }
    out_ch[0] = out + produced;
    produced += dsp_resampler_flush(&r, out_ch, out_cap - produced);

    f64 signal = 0.0;
    f64 noise = 0.0;
    f64 omega_out = 2.0 * DSP_PI * freq / (f64)TEST_DSP_RATE;
    u64 skip = 512;
    for (u64 n = skip; n + skip < produced; n += 1) {
        f64 ideal = dsp_sin_f64(omega_out * (f64)n);
        f64 error = (f64)out[n] - ideal;
        signal += ideal * ideal;
        noise += error * error;
    }
    if (noise <= 0.0) { return 200.0; }
    return 10.0 * dsp_log10_f64(signal / noise);
}

TEST(dsp_resample_sine_48k) {
    f64 snr = test_dsp_resample_snr(arena, 48000, 1000.0, 1);
    test_report("    48000 -> 44100, 1 kHz: SNR %f dB\n", snr);
    EXPECT(snr > 90.0);
}

TEST(dsp_resample_sine_96k) {
    f64 snr = test_dsp_resample_snr(arena, 96000, 1000.0, 1);
    test_report("    96000 -> 44100, 1 kHz: SNR %f dB\n", snr);
    EXPECT(snr > 90.0);
}

TEST(dsp_resample_sine_22k) {
    f64 snr = test_dsp_resample_snr(arena, 22050, 1000.0, 1);
    test_report("    22050 -> 44100, 1 kHz: SNR %f dB\n", snr);
    EXPECT(snr > 90.0);
}

TEST(dsp_resample_alias_20k) {
    // A 20 kHz tone at 48 kHz has an image at 28 kHz; decimating to 44.1 kHz
    // would fold it onto 16.1 kHz, in the middle of the audible band. That
    // image is what the stopband is for.
    DspResampler r;
    u64 in_count = 48000;
    dsp_resampler_init(&r, arena, 48000, TEST_DSP_RATE, 1, 4096, in_count);
    u64 out_cap = dsp_resampler_out_capacity(&r, in_count) + 4096;
    f32 *in = push_array_zero(arena, f32, in_count);
    f32 *out = push_array_zero(arena, f32, out_cap);
    test_dsp_fill_sine(in, in_count, 20000.0, 48000, 1.0, 0.0);

    f32 *in_ch[1];
    f32 *out_ch[1];
    u64 produced = 0;
    for (u64 done = 0; done < in_count; done += 4096) {
        u64 chunk = Min((u64)4096, in_count - done);
        in_ch[0] = in + done;
        out_ch[0] = out + produced;
        produced += dsp_resampler_process(&r, (const f32 *const *)in_ch, chunk, out_ch,
                                          out_cap - produced);
    }

    u64 skip = 1024;
    u64 span = produced - 2 * skip;
    f64 alias = test_dsp_tone_amp(out + skip, span, 16100.0, TEST_DSP_RATE);
    f64 alias_db = 20.0 * dsp_log10_f64(alias);
    test_report("    20 kHz image at 16.1 kHz: %f dBFS\n", alias_db);
    EXPECT(alias_db < -90.0);
}

// --- EBU R128 ---------------------------------------------------------------
// Feeds a series of constant-level 1 kHz segments, which is the shape of every
// Tech 3341 gating vector.
typedef struct TestR128Segment {
    f64 seconds;
    f64 db;  // DSP_DB_SILENCE for digital silence
} TestR128Segment;

static f32 test_dsp_r128_segments(Arena *arena, const TestR128Segment *segments, u32 count,
                                  f32 *out_dbtp) {
    u64 total = 0;
    for (u32 s = 0; s < count; s += 1) {
        total += (u64)(segments[s].seconds * (f64)TEST_DSP_RATE);
    }
    DspR128 state;
    dsp_r128_init(&state, arena, TEST_DSP_RATE, 2, total);
    dsp_r128_reset(&state);

    u64 block = 4410;
    f32 *left = push_array_zero(arena, f32, block);
    f32 *right = push_array_zero(arena, f32, block);
    const f32 *planar[2];
    planar[0] = left;
    planar[1] = right;

    f64 omega = 2.0 * DSP_PI * 1000.0 / (f64)TEST_DSP_RATE;
    u64 phase_index = 0;
    for (u32 s = 0; s < count; s += 1) {
        u64 frames = (u64)(segments[s].seconds * (f64)TEST_DSP_RATE);
        f64 amplitude =
            (segments[s].db <= (f64)DSP_DB_SILENCE) ? 0.0 : dsp_pow_f64(10.0, segments[s].db / 20.0);
        u64 done = 0;
        while (done < frames) {
            u64 chunk = Min(block, frames - done);
            for (u64 i = 0; i < chunk; i += 1) {
                f32 v = (f32)(amplitude * dsp_sin_f64(omega * (f64)(phase_index + i)));
                left[i] = v;
                right[i] = v;
            }
            dsp_r128_feed(&state, planar, chunk);
            phase_index += chunk;
            done += chunk;
        }
    }
    if (out_dbtp) { *out_dbtp = dsp_r128_true_peak_dbtp(&state); }
    return dsp_r128_integrated_lufs(&state);
}

TEST(dsp_r128_tech3341_levels) {
    TestR128Segment case1[1];
    case1[0].seconds = 20.0;
    case1[0].db = -23.0;
    f32 lufs = test_dsp_r128_segments(arena, case1, 1, 0);
    test_report("    3341-1: %f LUFS (expected -23.0)\n", (f64)lufs);
    EXPECT(lufs > -23.1f && lufs < -22.9f);

    TestR128Segment case2[1];
    case2[0].seconds = 20.0;
    case2[0].db = -33.0;
    lufs = test_dsp_r128_segments(arena, case2, 1, 0);
    test_report("    3341-2: %f LUFS (expected -33.0)\n", (f64)lufs);
    EXPECT(lufs > -33.1f && lufs < -32.9f);
}

TEST(dsp_r128_tech3341_gating) {
    // Case 3: 10 s at -36, 60 s at -23, 10 s at -36. The relative gate lands at
    // -34.2 LUFS, so the -36 segments drop out and the answer is -23.0.
    TestR128Segment case3[3];
    case3[0].seconds = 10.0;
    case3[0].db = -36.0;
    case3[1].seconds = 60.0;
    case3[1].db = -23.0;
    case3[2].seconds = 10.0;
    case3[2].db = -36.0;
    f32 lufs = test_dsp_r128_segments(arena, case3, 3, 0);
    test_report("    3341-3: %f LUFS (expected -23.0)\n", (f64)lufs);
    EXPECT(lufs > -23.1f && lufs < -22.9f);
}

TEST(dsp_r128_tech3341_absolute_gate) {
    // Case 4 adds two -72 LUFS shoulders, which the -70 LUFS absolute gate has
    // to drop before the relative gate is even computed.
    TestR128Segment case4[5];
    case4[0].seconds = 20.0;
    case4[0].db = -72.0;
    case4[1].seconds = 10.0;
    case4[1].db = -36.0;
    case4[2].seconds = 60.0;
    case4[2].db = -23.0;
    case4[3].seconds = 10.0;
    case4[3].db = -36.0;
    case4[4].seconds = 20.0;
    case4[4].db = -72.0;
    f32 lufs = test_dsp_r128_segments(arena, case4, 5, 0);
    test_report("    3341-4: %f LUFS (expected -23.0)\n", (f64)lufs);
    EXPECT(lufs > -23.1f && lufs < -22.9f);
}

TEST(dsp_r128_noise_floor) {
    // A track that is nothing but a -60 dBFS floor measures around -63 LUFS and
    // must not be gated out; the same floor next to real music must be.
    TestR128Segment floor_only[1];
    floor_only[0].seconds = 10.0;
    floor_only[0].db = -60.0;
    f32 quiet = test_dsp_r128_segments(arena, floor_only, 1, 0);
    EXPECT(quiet > -61.0f && quiet < -59.0f);

    TestR128Segment mixed[3];
    mixed[0].seconds = 10.0;
    mixed[0].db = -60.0;
    mixed[1].seconds = 30.0;
    mixed[1].db = -14.0;
    mixed[2].seconds = 10.0;
    mixed[2].db = -60.0;
    f32 lufs = test_dsp_r128_segments(arena, mixed, 3, 0);
    test_report("    -60 dBFS floor beside -14 dBFS: %f LUFS\n", (f64)lufs);
    EXPECT(lufs > -14.1f && lufs < -13.9f);

    // Digital silence produces no gated block at all.
    TestR128Segment silence[1];
    silence[0].seconds = 5.0;
    silence[0].db = (f64)DSP_DB_SILENCE;
    EXPECT(test_dsp_r128_segments(arena, silence, 1, 0) <= DSP_DB_SILENCE);
}

TEST(dsp_r128_true_peak) {
    // A sine at exactly fs/4 with a quarter-cycle offset never lands on its own
    // maximum: the samples sit at +-A/sqrt(2), 3.01 dB below a true peak of A.
    // That is the textbook inter-sample peak, and 0 dBTP is the right answer.
    DspR128 state;
    u64 frames = 44100;
    dsp_r128_init(&state, arena, TEST_DSP_RATE, 2, frames);
    dsp_r128_reset(&state);
    f32 *left = push_array_zero(arena, f32, frames);
    f32 *right = push_array_zero(arena, f32, frames);
    test_dsp_fill_sine(left, frames, (f64)TEST_DSP_RATE / 4.0, TEST_DSP_RATE, 1.0, DSP_PI / 4.0);
    mem_copy(right, left, frames * sizeof(f32));
    const f32 *planar[2];
    planar[0] = left;
    planar[1] = right;
    dsp_r128_feed(&state, planar, frames);
    f32 dbtp = dsp_r128_true_peak_dbtp(&state);
    test_report("    fs/4 inter-sample peak: %f dBTP (expected 0.0, samples at -3.01)\n",
                (f64)dbtp);
    EXPECT(dbtp > -0.2f && dbtp < 0.2f);

    // A 997 Hz tone at -6 dBFS is slow enough that its peak is nearly on a
    // sample: the oversampler must not invent headroom loss there either.
    DspR128 slow;
    dsp_r128_init(&slow, arena, TEST_DSP_RATE, 2, frames);
    dsp_r128_reset(&slow);
    test_dsp_fill_sine(left, frames, 997.0, TEST_DSP_RATE, 0.5, 0.0);
    mem_copy(right, left, frames * sizeof(f32));
    dsp_r128_feed(&slow, planar, frames);
    f32 slow_dbtp = dsp_r128_true_peak_dbtp(&slow);
    EXPECT(slow_dbtp > -6.3f && slow_dbtp < -5.8f);
}

TEST(dsp_r128_gain) {
    DspR128 state;
    u64 frames = (u64)TEST_DSP_RATE * 20u;
    dsp_r128_init(&state, arena, TEST_DSP_RATE, 2, frames);
    dsp_r128_reset(&state);
    u64 block = 4410;
    f32 *left = push_array_zero(arena, f32, block);
    f32 *right = push_array_zero(arena, f32, block);
    const f32 *planar[2];
    planar[0] = left;
    planar[1] = right;
    f64 omega = 2.0 * DSP_PI * 997.0 / (f64)TEST_DSP_RATE;
    f64 amplitude = dsp_pow_f64(10.0, -23.0 / 20.0);
    for (u64 done = 0; done < frames; done += block) {
        for (u64 i = 0; i < block; i += 1) {
            left[i] = (f32)(amplitude * dsp_sin_f64(omega * (f64)(done + i)));
            right[i] = left[i];
        }
        dsp_r128_feed(&state, planar, block);
    }
    // -23 LUFS toward -14 asks for +9 dB, and the peak (-23 dBTP) still has
    // room, so the whole +9 dB is granted.
    f32 gain = dsp_r128_gain_db(&state, -14.0f, -1.0f);
    EXPECT(gain > 8.8f && gain < 9.2f);

    // The same measurement with a ceiling below the peak: the gain comes down,
    // it is never turned into limiting.
    f32 capped = dsp_r128_gain_db(&state, -14.0f, -20.0f);
    f32 peak = dsp_r128_true_peak_dbtp(&state);
    EXPECT(capped < gain);
    EXPECT(test_dsp_close((f64)(peak + capped), -20.0, 0.05));
}

// --- editing ----------------------------------------------------------------
TEST(dsp_edit_downmix) {
    u64 count = 1000;
    f32 *left = push_array(arena, f32, count);
    f32 *right = push_array(arena, f32, count);
    f32 *dst = push_array(arena, f32, count);
    for (u64 i = 0; i < count; i += 1) {
        left[i] = (f32)i;
        right[i] = -(f32)i * 0.5f;
    }
    dsp_downmix_mono(dst, left, right, count);
    b32 exact = 1;
    for (u64 i = 0; i < count; i += 1) {
        if (dst[i] != (left[i] + right[i]) * 0.5f) { exact = 0; }
    }
    EXPECT(exact);
    // In place, which is how the pipeline calls it.
    dsp_downmix_mono(left, left, right, count);
    EXPECT(left[999] == dst[999]);
}

TEST(dsp_edit_trim) {
    u32 rate = TEST_DSP_RATE;
    // The envelope window is 1 ms (44 frames at 44.1 kHz) and the trim is
    // quantised to it, so the segments are laid out on window boundaries and
    // the expected lengths below are exact rather than approximate.
    u64 window = rate / 1000u;
    u64 head = window * 1000u;   // ~1 s of silence
    u64 body = window * 2000u;   // ~2 s of tone
    u64 tail = window * 1500u;
    u64 total = head + body + tail;
    f32 *signal = push_array_zero(arena, f32, total);
    test_dsp_fill_sine(signal + head, body, 440.0, rate, 0.5, 0.0);

    DspTrimScan scan;
    dsp_trim_scan_init(&scan, rate, DSP_TRIM_THRESHOLD_DB, DSP_TRIM_HYSTERESIS_MS);
    const f32 *planar[1];
    for (u64 done = 0; done < total; done += 4096) {
        u64 chunk = Min((u64)4096, total - done);
        planar[0] = signal + done;
        dsp_trim_scan_feed(&scan, planar, 1, chunk);
    }
    u64 begin = 0;
    u64 end = 0;
    dsp_trim_scan_result(&scan, total, &begin, &end);
    // The envelope windows are 1 ms and both edges of the tone sit on a
    // millisecond boundary, so the kept window is exact - no fudge factor.
    test_report("    trim: kept [%llu, %llu) of %llu\n", begin, end, total);
    EXPECT(begin == head);
    EXPECT(end == head + body);

    // Silence alone yields an empty window.
    DspTrimScan quiet;
    dsp_trim_scan_init(&quiet, rate, DSP_TRIM_THRESHOLD_DB, DSP_TRIM_HYSTERESIS_MS);
    mem_zero(signal, total * sizeof(f32));
    planar[0] = signal;
    dsp_trim_scan_feed(&quiet, planar, 1, total);
    dsp_trim_scan_result(&quiet, total, &begin, &end);
    EXPECT(begin == 0 && end == 0);
}

TEST(dsp_edit_fade) {
    u64 total = 10000;
    f32 *data = push_array(arena, f32, total);
    for (u64 i = 0; i < total; i += 1) { data[i] = 1.0f; }
    f32 *planar[1];
    planar[0] = data;

    DspFade fade;
    fade.in_length = 1000;
    fade.out_length = 2000;
    fade.out_start = total - 2000;
    dsp_fade_apply(&fade, planar, 1, total, 0);

    EXPECT(data[0] == 0.0f);
    EXPECT(test_dsp_close((f64)data[500], 0.5, 1e-6));   // half way: cos(pi/2)
    EXPECT(test_dsp_close((f64)data[1000], 1.0, 1e-6));  // the fade is over
    EXPECT(data[5000] == 1.0f);
    EXPECT(test_dsp_close((f64)data[total - 1000], 0.5, 1e-6));
    EXPECT(data[total - 1] < 0.002f);

    // Block by block gives the same curve as one pass: the fade is stateless,
    // it reads the stream position.
    for (u64 i = 0; i < total; i += 1) { data[i] = 1.0f; }
    for (u64 done = 0; done < total; done += 333) {
        u64 chunk = Min((u64)333, total - done);
        planar[0] = data + done;
        dsp_fade_apply(&fade, planar, 1, chunk, done);
    }
    EXPECT(test_dsp_close((f64)data[500], 0.5, 1e-6));
    EXPECT(test_dsp_close((f64)data[total - 1000], 0.5, 1e-6));
}

// --- dither and wire formats ------------------------------------------------
TEST(dsp_dither_statistics) {
    // Dithered quantisation has a total error variance of exactly 1/4 LSB^2
    // (1/12 of quantisation plus 1/6 of TPDF) and a mean of zero. That the mean
    // is zero is the whole point: it is what stops the error correlating with
    // the signal on a fade.
    u64 frames = 200000;
    f32 *signal = push_array(arena, f32, frames);
    test_dsp_fill_sine(signal, frames, 31.0, TEST_DSP_RATE, 0.25, 0.0);
    i16 *out = push_array(arena, i16, frames);
    const f32 *planar[1];
    planar[0] = signal;

    DspDither dither;
    dsp_dither_init(&dither, 1, 0, 12345u);
    dsp_quantize_s16(&dither, planar, 1, frames, out);

    f64 sum = 0.0;
    f64 sum_sq = 0.0;
    for (u64 i = 0; i < frames; i += 1) {
        f64 error = (f64)out[i] - (f64)signal[i] * 32768.0;
        sum += error;
        sum_sq += error * error;
    }
    f64 mean = sum / (f64)frames;
    f64 variance = sum_sq / (f64)frames - mean * mean;
    test_report("    dither: mean %f LSB, variance %f LSB^2 (expected 0 and 0.25)\n", mean,
                variance);
    EXPECT(test_dsp_close(mean, 0.0, 0.01));
    EXPECT(variance > 0.22 && variance < 0.28);

    // Noise shaping keeps the mean at zero and raises the total noise power,
    // which is exactly the trade it makes: more energy, moved out of the way.
    DspDither shaped;
    dsp_dither_init(&shaped, 1, 1, 12345u);
    dsp_quantize_s16(&shaped, planar, 1, frames, out);
    sum = 0.0;
    sum_sq = 0.0;
    for (u64 i = 0; i < frames; i += 1) {
        f64 error = (f64)out[i] - (f64)signal[i] * 32768.0;
        sum += error;
        sum_sq += error * error;
    }
    mean = sum / (f64)frames;
    variance = sum_sq / (f64)frames - mean * mean;
    test_report("    dither + shaping: mean %f LSB, variance %f LSB^2\n", mean, variance);
    EXPECT(test_dsp_close(mean, 0.0, 0.02));
    // The dither passes through unshaped and the quantisation error is shaped
    // by (1 - z^-1): 1/6 + 2 x 1/12 = 1/3 LSB^2.
    EXPECT(variance > 0.30 && variance < 0.37);
}

TEST(dsp_dither_bit_exact) {
    // No dither: an s16 that went through f32 comes back byte for byte, which
    // is the bit-perfect promise of research/03 s6.9.
    u64 frames = 70000;
    f32 *signal = push_array(arena, f32, frames);
    i16 *reference = push_array(arena, i16, frames);
    u32 rng = 7u;
    for (u64 i = 0; i < frames; i += 1) {
        rng = rng * 1664525u + 1013904223u;
        i16 sample = (i16)(u16)(rng >> 16);
        reference[i] = sample;
        signal[i] = (f32)sample * (1.0f / 32768.0f);
    }
    i16 *out = push_array(arena, i16, frames);
    const f32 *planar[1];
    planar[0] = signal;
    DspDither dither;
    dsp_dither_init(&dither, 0, 0, 0);
    dsp_quantize_s16(&dither, planar, 1, frames, out);
    EXPECT(mem_cmp(out, reference, frames * sizeof(i16)) == 0);

    // Both rails clamp instead of wrapping.
    f32 rails[4];
    rails[0] = 1.0f;
    rails[1] = -1.0f;
    rails[2] = 2.0f;
    rails[3] = -2.0f;
    planar[0] = rails;
    i16 railed[4];
    dsp_quantize_s16(&dither, planar, 1, 4, railed);
    EXPECT(railed[0] == 32767);
    EXPECT(railed[1] == -32768);
    EXPECT(railed[2] == 32767);
    EXPECT(railed[3] == -32768);
}

typedef struct TestSink {
    u8 *data;
    u64 size;
    u64 cap;
    b32 count_only;
    b32 fail;
} TestSink;

static b32 test_sink_write(void *user, const u8 *bytes, u64 size) {
    TestSink *sink = (TestSink *)user;
    if (sink->fail) { return 0; }
    if (!sink->count_only) {
        if (sink->size + size > sink->cap) { return 0; }
        mem_copy(sink->data + sink->size, bytes, size);
    }
    sink->size += size;
    return 1;
}

TEST(dsp_wire_formats) {
    // Big-endian bytes, exactly.
    i16 samples[4];
    samples[0] = 0x0102;
    samples[1] = (i16)-129;
    samples[2] = 0;
    samples[3] = (i16)-32768;
    u8 bytes[8];
    dsp_s16_to_be(bytes, samples, 4);
    EXPECT(bytes[0] == 0x01 && bytes[1] == 0x02);
    EXPECT(bytes[2] == 0xFF && bytes[3] == 0x7F);
    EXPECT(bytes[4] == 0x00 && bytes[5] == 0x00);
    EXPECT(bytes[6] == 0x80 && bytes[7] == 0x00);

    // SP framing: 2048-byte frames, the last one zero padded.
    TestSink sink;
    StructZero(&sink);
    sink.cap = KB(64);
    sink.data = push_array(arena, u8, sink.cap);
    DspSpWriter writer;
    dsp_sp_writer_init(&writer, test_sink_write, &sink);
    u64 sample_count = 1030 * 2;  // 4120 bytes: two frames and a bit
    i16 *pcm = push_array(arena, i16, sample_count);
    for (u64 i = 0; i < sample_count; i += 1) { pcm[i] = (i16)(i + 1); }
    EXPECT(dsp_sp_writer_push(&writer, pcm, sample_count));
    EXPECT(sink.size == 4096);  // only the two complete frames left so far
    EXPECT(dsp_sp_writer_finish(&writer));
    EXPECT(sink.size == 3 * DSP_SP_FRAME_BYTES);
    EXPECT(writer.frames_written == 3);
    EXPECT(sink.data[0] == 0x00 && sink.data[1] == 0x01);
    // 4120 audio bytes: everything past that in the last frame is padding.
    EXPECT(sink.data[4119] != 0 || sink.data[4118] != 0);
    b32 padded = 1;
    for (u64 i = 4120; i < 3 * DSP_SP_FRAME_BYTES; i += 1) {
        if (sink.data[i] != 0) { padded = 0; }
    }
    EXPECT(padded);

    // WAV header, byte for byte.
    u8 header[DSP_WAV_HEADER_BYTES];
    dsp_wav_header(header, 44100, 2, 1000);
    EXPECT(header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F');
    EXPECT(header[4] == (u8)(1036 & 0xFF) && header[5] == (u8)((1036 >> 8) & 0xFF));
    EXPECT(header[20] == 1 && header[21] == 0);
    EXPECT(header[22] == 2 && header[23] == 0);
    EXPECT(header[24] == 0x44 && header[25] == 0xAC && header[26] == 0 && header[27] == 0);
    EXPECT(header[28] == 0x10 && header[29] == 0xB1 && header[30] == 0x02);
    EXPECT(header[32] == 4 && header[34] == 16);
    EXPECT(header[36] == 'd' && header[39] == 'a');
    EXPECT(header[40] == (u8)(1000 & 0xFF) && header[41] == (u8)((1000 >> 8) & 0xFF));
}

// --- pipeline ---------------------------------------------------------------
typedef struct TestSource {
    u64 total;
    u64 pos;
    u32 rate;
    u32 channels;
    f64 freq;
    f64 amplitude;
    const i16 *pcm;         // when set, replayed verbatim through f32
    u64 lead_silence;       // frames of silence before the tone
    u64 tail_silence;       // frames of silence after it
    volatile u32 *cancel;   // set once `cancel_after` frames have been read
    u64 cancel_after;
    u64 reads;
} TestSource;

static u64 test_source_read(void *user, f32 *const *planar, u64 max_frames) {
    TestSource *source = (TestSource *)user;
    u64 remaining = source->total - source->pos;
    u64 count = Min(remaining, max_frames);
    if (count == 0) { return 0; }
    f64 omega = 2.0 * DSP_PI * source->freq / (f64)source->rate;
    for (u64 i = 0; i < count; i += 1) {
        u64 index = source->pos + i;
        f32 value;
        if (source->pcm) {
            value = (f32)source->pcm[index * source->channels] * (1.0f / 32768.0f);
        } else if (index < source->lead_silence ||
                   index >= source->total - source->tail_silence) {
            value = 0.0f;
        } else {
            value = (f32)(source->amplitude * dsp_sin_f64(omega * (f64)index));
        }
        planar[0][i] = value;
        if (source->channels == 2) {
            planar[1][i] = source->pcm
                               ? (f32)source->pcm[index * 2 + 1] * (1.0f / 32768.0f)
                               : value;
        }
    }
    source->pos += count;
    source->reads += 1;
    if (source->cancel && source->pos >= source->cancel_after) {
        os_atomic_store_u32(source->cancel, 1);
    }
    return count;
}

static b32 test_source_rewind(void *user) {
    TestSource *source = (TestSource *)user;
    source->pos = 0;
    return 1;
}

TEST(dsp_pipeline_bypass) {
    // 44.1 kHz stereo, nothing enabled: the WAV payload must be the input s16
    // back, byte for byte. That is the acceptance criterion of T-041.
    u64 frames = 100000;
    i16 *pcm = push_array(arena, i16, frames * 2);
    u32 rng = 99u;
    for (u64 i = 0; i < frames * 2; i += 1) {
        rng = rng * 1664525u + 1013904223u;
        pcm[i] = (i16)(u16)(rng >> 16);
    }

    TestSource source;
    StructZero(&source);
    source.total = frames;
    source.rate = 44100;
    source.channels = 2;
    source.pcm = pcm;

    TestSink sink;
    StructZero(&sink);
    sink.cap = DSP_WAV_HEADER_BYTES + frames * 4 + 1024;
    sink.data = push_array(arena, u8, sink.cap);

    PipelineTask task;
    StructZero(&task);
    pipeline_config_defaults(&task.config);
    task.config.format = PIPELINE_FORMAT_WAV;
    task.source.read = test_source_read;
    task.source.rewind = test_source_rewind;
    task.source.user = &source;
    task.source.sample_rate = 44100;
    task.source.channels = 2;
    task.source.total_frames = frames;
    task.write = test_sink_write;
    task.write_user = &sink;
    task.arena = arena;
    pipeline_run(&task);

    EXPECT(task.result.status == PIPELINE_OK);
    EXPECT(task.result.bit_perfect);
    EXPECT(task.result.out_frames == frames);
    EXPECT(sink.size == DSP_WAV_HEADER_BYTES + frames * 4);
    EXPECT(mem_cmp(sink.data + DSP_WAV_HEADER_BYTES, pcm, frames * 4) == 0);
}

TEST(dsp_pipeline_trim_fade_gap) {
    u32 rate = 44100;
    u64 window = rate / 1000u;   // the trim envelope window: see dsp_edit_trim
    u64 lead = window * 1000u;
    u64 tone = window * 2000u;
    u64 tail = window * 1000u;
    u64 frames = lead + tone + tail;

    TestSource source;
    StructZero(&source);
    source.total = frames;
    source.rate = rate;
    source.channels = 1;
    source.freq = 440.0;
    source.amplitude = 0.5;
    source.lead_silence = lead;
    source.tail_silence = tail;

    TestSink sink;
    StructZero(&sink);
    sink.count_only = 1;

    PipelineTask task;
    StructZero(&task);
    pipeline_config_defaults(&task.config);
    task.config.format = PIPELINE_FORMAT_WAV;
    task.config.trim = 1;
    task.config.gap_s = 0.5f;
    task.source.read = test_source_read;
    task.source.rewind = test_source_rewind;
    task.source.user = &source;
    task.source.sample_rate = rate;
    task.source.channels = 1;
    task.source.total_frames = frames;
    task.write = test_sink_write;
    task.write_user = &sink;
    task.arena = arena;
    pipeline_run(&task);

    EXPECT(task.result.status == PIPELINE_OK);
    u64 gap = rate / 2u;
    // Exact: the tone plus the requested half second of silence.
    test_report("    trim+gap: %llu frames, expected %llu\n", task.result.out_frames, tone + gap);
    EXPECT(task.result.out_frames == tone + gap);
    EXPECT(sink.size == DSP_WAV_HEADER_BYTES + task.result.out_frames * 2);

    // A negative gap eats the tail instead of adding silence.
    source.pos = 0;
    sink.size = 0;
    task.config.gap_s = -0.25f;
    pipeline_run(&task);
    EXPECT(task.result.status == PIPELINE_OK);
    EXPECT(task.result.out_frames == tone - rate / 4u);
}

TEST(dsp_pipeline_normalize_sp) {
    u32 rate = 48000;
    u64 frames = (u64)rate * 6u;
    TestSource source;
    StructZero(&source);
    source.total = frames;
    source.rate = rate;
    source.channels = 2;
    source.freq = 997.0;
    source.amplitude = dsp_pow_f64(10.0, -23.0 / 20.0);

    TestSink sink;
    StructZero(&sink);
    sink.count_only = 1;

    PipelineTask task;
    StructZero(&task);
    pipeline_config_defaults(&task.config);
    task.config.normalize = 1;
    task.config.dither = 1;
    task.source.read = test_source_read;
    task.source.rewind = test_source_rewind;
    task.source.user = &source;
    task.source.sample_rate = rate;
    task.source.channels = 2;
    task.source.total_frames = frames;
    task.write = test_sink_write;
    task.write_user = &sink;
    task.arena = arena;
    pipeline_run(&task);

    EXPECT(task.result.status == PIPELINE_OK);
    test_report("    pipeline 48k -> SP: %f LUFS, %f dBTP, gain %f dB, %llu frames\n",
                (f64)task.result.measured_lufs, (f64)task.result.measured_dbtp,
                (f64)task.result.applied_gain_db, task.result.sp_frames);
    EXPECT(task.result.measured_lufs > -23.2f && task.result.measured_lufs < -22.8f);
    EXPECT(task.result.applied_gain_db > 8.7f && task.result.applied_gain_db < 9.3f);
    EXPECT(!task.result.bit_perfect);

    u64 expected_out = dsp_resampler_expected_out(rate, 44100, frames);
    EXPECT(task.result.out_frames == expected_out);
    u64 audio_bytes = expected_out * 2u * 2u;
    u64 expected_frames = (audio_bytes + DSP_SP_FRAME_BYTES - 1) / DSP_SP_FRAME_BYTES;
    EXPECT(task.result.sp_frames == expected_frames);
    EXPECT(sink.size == expected_frames * DSP_SP_FRAME_BYTES);
}

TEST(dsp_pipeline_four_minutes) {
    u32 rate = 44100;
    u64 frames = (u64)rate * 240u;
    TestSource source;
    StructZero(&source);
    source.total = frames;
    source.rate = rate;
    source.channels = 2;
    source.freq = 440.0;
    source.amplitude = 0.3;

    TestSink sink;
    StructZero(&sink);
    sink.count_only = 1;

    volatile long long progress = 0;
    PipelineTask task;
    StructZero(&task);
    pipeline_config_defaults(&task.config);
    task.config.dither = 1;
    task.source.read = test_source_read;
    task.source.rewind = test_source_rewind;
    task.source.user = &source;
    task.source.sample_rate = rate;
    task.source.channels = 2;
    task.source.total_frames = frames;
    task.write = test_sink_write;
    task.write_user = &sink;
    task.arena = arena;
    task.progress = &progress;
    pipeline_run(&task);

    EXPECT(task.result.status == PIPELINE_OK);
    EXPECT(task.result.out_frames == frames);
    EXPECT((u64)progress == frames);
    EXPECT(task.result.sp_frames == (frames * 4 + DSP_SP_FRAME_BYTES - 1) / DSP_SP_FRAME_BYTES);
}

TEST(dsp_pipeline_cancel) {
    u32 rate = 44100;
    u64 frames = (u64)rate * 240u;
    volatile u32 cancel = 0;
    TestSource source;
    StructZero(&source);
    source.total = frames;
    source.rate = rate;
    source.channels = 2;
    source.freq = 440.0;
    source.amplitude = 0.3;
    source.cancel = &cancel;
    source.cancel_after = frames / 2u;

    TestSink sink;
    StructZero(&sink);
    sink.count_only = 1;

    PipelineTask task;
    StructZero(&task);
    pipeline_config_defaults(&task.config);
    task.source.read = test_source_read;
    task.source.rewind = test_source_rewind;
    task.source.user = &source;
    task.source.sample_rate = rate;
    task.source.channels = 2;
    task.source.total_frames = frames;
    task.write = test_sink_write;
    task.write_user = &sink;
    task.arena = arena;
    task.cancel = &cancel;
    pipeline_run(&task);

    EXPECT(task.result.status == PIPELINE_CANCELLED);
    // It stopped where it was told to, give or take the block it was inside.
    EXPECT(task.result.out_frames >= frames / 2u);
    EXPECT(task.result.out_frames < frames / 2u + 2u * PIPELINE_BLOCK_FRAMES);
    // The arena is handed back whatever happened: a cancelled job leaks nothing.
    EXPECT(arena_pos(arena) < KB(64));
}

TEST(dsp_pipeline_jobs) {
    // Four tracks, one job each, each with its own arena: the shape T-043 will
    // use for a whole disc.
    u32 count = 4;
    u32 rate = 44100;
    u64 frames = (u64)rate * 5u;
    TestSource *sources = push_array_zero(arena, TestSource, count);
    TestSink *sinks = push_array_zero(arena, TestSink, count);
    PipelineTask *tasks = push_array_zero(arena, PipelineTask, count);
    Arena *arenas[4];

    for (u32 i = 0; i < count; i += 1) {
        sources[i].total = frames;
        sources[i].rate = rate;
        sources[i].channels = 2;
        sources[i].freq = 200.0 * (f64)(i + 1);
        sources[i].amplitude = 0.2;
        sinks[i].count_only = 1;
        arenas[i] = arena_alloc(MB(32));

        pipeline_config_defaults(&tasks[i].config);
        tasks[i].config.normalize = 1;
        tasks[i].config.dither = 1;
        tasks[i].source.read = test_source_read;
        tasks[i].source.rewind = test_source_rewind;
        tasks[i].source.user = &sources[i];
        tasks[i].source.sample_rate = rate;
        tasks[i].source.channels = 2;
        tasks[i].source.total_frames = frames;
        tasks[i].write = test_sink_write;
        tasks[i].write_user = &sinks[i];
        tasks[i].arena = arenas[i];
    }

    jobs_init(0);
    pipeline_run_many(tasks, count);
    jobs_shutdown();

    for (u32 i = 0; i < count; i += 1) {
        EXPECT(tasks[i].result.status == PIPELINE_OK);
        EXPECT(tasks[i].result.out_frames == frames);
        EXPECT(tasks[i].result.measured_lufs > -20.0f && tasks[i].result.measured_lufs < -5.0f);
        arena_release(arenas[i]);
    }
}

static void test_dsp_run_all(void) {
    RUN(dsp_math);
    RUN(dsp_resample_impulse);
    RUN(dsp_resample_sine_48k);
    RUN(dsp_resample_sine_96k);
    RUN(dsp_resample_sine_22k);
    RUN(dsp_resample_alias_20k);
    RUN(dsp_r128_tech3341_levels);
    RUN(dsp_r128_tech3341_gating);
    RUN(dsp_r128_tech3341_absolute_gate);
    RUN(dsp_r128_noise_floor);
    RUN(dsp_r128_true_peak);
    RUN(dsp_r128_gain);
    RUN(dsp_edit_downmix);
    RUN(dsp_edit_trim);
    RUN(dsp_edit_fade);
    RUN(dsp_dither_statistics);
    RUN(dsp_dither_bit_exact);
    RUN(dsp_wire_formats);
    RUN(dsp_pipeline_bypass);
    RUN(dsp_pipeline_trim_fade_gap);
    RUN(dsp_pipeline_normalize_sp);
    RUN(dsp_pipeline_four_minutes);
    RUN(dsp_pipeline_cancel);
    RUN(dsp_pipeline_jobs);
}
