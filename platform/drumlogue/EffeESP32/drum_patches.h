#pragma once

/**
 * @file drum_patches.h
 * @brief Instrument patch table — auto-generated, DO NOT EDIT BY HAND.
 *
 * Source data: copych/ESP32-S3_FM_Drum_Synth, FMDrums/data/drumkits/
 *              Drumkit_default.json (commit 6e47275).  MIT License.
 * Generator:   tools/gen_patches.py
 *
 * Layout mirrors the original FmDrumPatch (FmPatch.h): a flat struct of
 * fixed parameters.  Selecting an instrument copies one of these structs
 * into the synth working cache; the UI then edits the cached copy.
 */

#include "fm_voice6.h"

#define DRUM_INST_COUNT 59

static const fm_drum_patch_t g_drum_patches[DRUM_INST_COUNT] = {
  /* BassDrum (slot 35, GM Acoustic Bass Drum) */
  {
    3, 32.0f, 1.68f, 0.0f,
    0.0f, 0.0f, 0.6005f, 0.0f, 0.6005f,
    0.5f, 0, 8444.0f, 0.32f, 0.0f,
    { { 0.05f, 0.0f, 0.0f, 1.005f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.18f, 0.0f, 0.0f, 0.68f, WF_COSINE },
      { 0.15f, 0.0f, 0.0f, 0.06f, WF_COSINE },
      { 0.07f, 0.0f, 0.9f, 0.43f, WF_SAW } }
  },
  /* Kick (slot 36, GM Bass Drum 1) */
  {
    3, 46.0f, 1.6f, 0.0f,
    0.001f, 0.005f, 0.795f, 0.0f, 0.77f,
    0.5f, 0, 4929.0f, 0.01f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.15f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.05f, 1.5f, 0.0f, 0.27f, WF_SINE },
      { 0.0f, 0.5f, 0.0f, 0.23f, WF_SAW } }
  },
  /* SideStick (slot 37, GM Side Stick) */
  {
    3, 290.0f, 1.6f, 0.0f,
    0.0f, 0.0f, 0.08f, 0.0f, 0.08f,
    0.5f, 1, 549.0f, 0.0f, 0.71f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_COSINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.7f, 0.0f, 6.4f, 0.71f, WF_SAW },
      { 1.11f, 0.0f, 5.8f, 0.08f, WF_SINE } }
  },
  /* AccSnare (slot 38, GM Acoustic Snare) */
  {
    3, 222.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.85f, 0.0f, 0.85f,
    0.5f, 1, 5868.0f, 0.01f, 0.01f,
    { { 1.0f, 0.0f, 0.0f, 0.95f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 6.7f, 0.36f, WF_SINE },
      { 1.0f, 0.0f, 8.2f, 0.8f, WF_SINE },
      { 0.79f, 0.0f, 6.35f, 0.01f, WF_SINE } }
  },
  /* Hand Claps (slot 39, GM Hand Clap) */
  {
    14, 124.0f, 1.6f, 0.0f,
    0.02f, 0.007f, 0.254f, 0.0f, 0.254f,
    0.8f, 1, 1285.0f, 0.53f, 0.52f,
    { { 1.005f, 0.0f, 7.15f, 1.005f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.92f, 0.0f, 0.0f, 0.67f, WF_SAW },
      { 1.005f, 0.0f, 7.25f, 1.005f, WF_SINE },
      { 1.04f, 0.0f, 4.2f, 0.58f, WF_SINE } }
  },
  /* Snare Body (slot 40, GM Electric Snare) */
  {
    3, 260.0f, 1.0f, 0.0f,
    0.002f, 0.01f, 0.25f, 0.0f, 0.25f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_TRIANGLE },
      { 0.0f, 0.0f, 0.0f, 0.11f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 4.3f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 7.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.0f, WF_SINE } }
  },
  /* Tom (slot 41, GM Low Floor Tom) */
  {
    3, 65.0f, 1.0f, -0.21f,
    0.0f, 0.01f, 0.828f, 0.005f, 0.828f,
    0.5f, 0, 16000.0f, 0.01f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.4f, WF_SINE },
      { 1.0f, 0.0f, 1.0f, 0.075f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.8f, WF_TRIANGLE },
      { 0.0f, 1.3f, 0.0f, 0.61f, WF_SAW },
      { 0.79f, 0.0f, 0.0f, 0.09f, WF_SINE } }
  },
  /* Hi Hat (slot 42, GM Closed Hi-Hat) */
  {
    2, 1817.0f, 1.1f, 0.16f,
    0.01f, 0.0f, 0.173f, 0.0f, 0.181f,
    0.5f, 1, 4820.0f, 0.0f, 1.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 4.0f, 0.0f, 3.0f, 0.8f, WF_SINE } }
  },
  /* Tom (slot 43, GM High Floor Tom) */
  {
    3, 100.0f, 1.0f, -0.2f,
    0.0f, 0.01f, 0.828f, 0.005f, 0.828f,
    0.5f, 0, 16000.0f, 0.01f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.48f, WF_TRIANGLE },
      { 1.0f, 0.0f, 1.0f, 0.075f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.79f, WF_TRIANGLE },
      { 0.0f, -2.5f, 0.0f, 0.4f, WF_SAW },
      { 0.5f, 0.0f, 0.0f, 0.02f, WF_SINE } }
  },
  /* Hi Hat (slot 44, GM Pedal Hi-Hat) */
  {
    2, 1817.0f, 0.78f, 0.15f,
    0.025f, 0.004f, 0.205f, 0.0f, 0.205f,
    0.5f, 1, 4820.0f, 0.0f, 1.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 4.0f, 0.0f, 3.0f, 0.8f, WF_SINE } }
  },
  /* Tom (slot 45, GM Low Tom) */
  {
    3, 135.0f, 1.0f, -0.15f,
    0.0f, 0.01f, 0.828f, 0.005f, 0.828f,
    0.5f, 0, 16000.0f, 0.01f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.33f, WF_TRIANGLE },
      { 1.0f, 0.0f, 1.0f, 0.075f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.79f, WF_TRIANGLE },
      { 0.0f, -2.5f, 0.0f, 0.4f, WF_SAW },
      { 0.5f, 0.0f, 0.0f, 0.02f, WF_SINE } }
  },
  /* Hi Hat (slot 46, GM Open Hi-Hat) */
  {
    3, 3931.0f, 1.0f, 0.15f,
    0.01f, 0.042f, 1.325f, 0.0f, 1.332f,
    0.5f, 1, 4337.0f, 0.0f, 1.0f,
    { { 1.02f, 0.0f, 0.0f, 0.39f, WF_SQUARE },
      { 1.16f, 0.0f, 4.7f, 0.85f, WF_SINE },
      { 2.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.09f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.88f, 0.0f, 0.0f, 0.54f, WF_COSINE } }
  },
  /* Tom (slot 47, GM Low-Mid Tom) */
  {
    3, 210.0f, 1.0f, -0.1f,
    0.0f, 0.01f, 0.828f, 0.005f, 0.828f,
    0.5f, 0, 16000.0f, 0.01f, 0.0f,
    { { 0.68f, 0.0f, 0.0f, 0.32f, WF_TRIANGLE },
      { 1.0f, 0.0f, 1.0f, 0.075f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.79f, WF_TRIANGLE },
      { 0.0f, -2.5f, 0.0f, 0.4f, WF_SAW },
      { 0.5f, 0.0f, 0.0f, 0.01f, WF_SINE } }
  },
  /* Tom (slot 48, GM Hi-Mid Tom) */
  {
    3, 320.0f, 1.0f, -0.06f,
    0.0f, 0.01f, 0.828f, 0.005f, 0.828f,
    0.5f, 0, 16000.0f, 0.01f, 0.0f,
    { { 0.71f, 0.0f, 0.0f, 0.13f, WF_TRIANGLE },
      { 1.0f, 0.0f, 1.0f, 0.075f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.79f, WF_TRIANGLE },
      { 0.0f, -2.5f, 0.0f, 0.4f, WF_SAW },
      { 0.5f, 0.0f, 0.0f, 0.01f, WF_TRIANGLE } }
  },
  /* Crash 1 (slot 49, GM Crash Cymbal 1) */
  {
    6, 1866.0f, 0.7f, -0.18f,
    0.01f, 0.01f, 6.0f, 0.0f, 6.0f,
    0.5f, 1, 1300.0f, 0.0f, 1.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.11f, 0.0f, 0.0f, 0.42f, WF_SQUARE },
      { 1.57f, 0.0f, 8.0f, 0.8f, WF_SINE },
      { 1.0f, 4.2f, 5.3f, 0.38f, WF_SINE },
      { 1.0f, 0.0f, 3.8f, 0.5f, WF_SQUARE },
      { 2.11f, 0.0f, 0.0f, 0.25f, WF_SQUARE } }
  },
  /* Tom (slot 50, GM High Tom) */
  {
    3, 400.0f, 1.0f, 0.0f,
    0.0f, 0.01f, 0.655f, 0.005f, 0.671f,
    0.5f, 0, 16000.0f, 0.01f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.12f, WF_TRIANGLE },
      { 1.0f, 0.0f, 1.0f, 0.075f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.79f, WF_TRIANGLE },
      { 0.0f, -2.5f, 0.0f, 0.4f, WF_SAW },
      { 0.42f, 0.0f, 0.0f, 0.01f, WF_SINE } }
  },
  /* Closed Hat (slot 51, GM Ride Cymbal 1) */
  {
    12, 991.0f, 1.54f, 0.14f,
    0.001f, 0.005f, 4.398f, 0.0f, 4.408f,
    0.5f, 1, 9751.0f, 0.0f, 0.92f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.07f, 0.0f, 0.0f, 0.22f, WF_SQUARE },
      { 1.03f, 0.0f, 0.0f, 0.29f, WF_SQUARE },
      { 1.54f, 0.0f, 0.0f, 0.34f, WF_SQUARE },
      { 0.99f, 0.0f, 0.0f, 0.08f, WF_SQUARE },
      { 0.55f, 0.0f, 0.0f, 0.12f, WF_SINE } }
  },
  /* Deep Tom (slot 52, GM Chinese Cymbal) */
  {
    3, 1163.0f, 1.5f, -0.1f,
    0.029f, 0.003f, 3.128f, 0.0f, 3.128f,
    0.5f, 1, 13262.0f, 0.0f, 0.01f,
    { { 1.0f, 0.0f, 6.2f, 0.35f, WF_SINE },
      { 1.04f, 0.0f, 6.8f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.01f, 0.0f, 5.1f, 0.47f, WF_SINE },
      { 1.61f, 0.0f, 0.0f, 0.26f, WF_SINE },
      { 2.44f, 0.0f, 0.0f, 0.8f, WF_SQUARE } }
  },
  /* Snare Body (slot 53, GM Ride Bell) */
  {
    10, 2178.0f, 0.32f, -0.11f,
    0.004f, 0.0f, 6.113f, 0.0f, 6.156f,
    0.5f, 1, 3779.0f, 0.01f, 1.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_COSINE },
      { 1.46f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.96f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.94f, 0.0f, 0.0f, 0.03f, WF_SINE },
      { 0.95f, 0.0f, 0.0f, 0.03f, WF_SINE },
      { 1.4f, 0.0f, 0.0f, 0.03f, WF_SINE } }
  },
  /* Snare Noise (slot 54, GM Tambourine) */
  {
    14, 8474.0f, 1.02f, 0.0f,
    0.034f, 0.009f, 0.628f, 0.0f, 0.63f,
    0.5f, 1, 5573.0f, 0.0f, 1.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.4f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 0.01f, -5.1f, 0.0f, 0.26f, WF_TRIANGLE },
      { 0.01f, 0.0f, 0.0f, 0.35f, WF_TRIANGLE },
      { 1.39f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 2.37f, 0.0f, 0.0f, 0.05f, WF_SQUARE } }
  },
  /* Cymbal (slot 55, GM Splash Cymbal) */
  {
    11, 1284.0f, 1.4f, -0.08f,
    0.0f, 0.01f, 1.5f, 0.0f, 1.5f,
    0.5f, 1, 4125.0f, 0.01f, 1.0f,
    { { 0.97f, 0.0f, 6.1f, 0.8f, WF_SINE },
      { 1.06f, -4.3f, 3.1f, 0.04f, WF_SINE },
      { 2.38f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.18f, 0.0f, 8.7f, 0.19f, WF_SINE },
      { 2.39f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.28f, 0.0f, 5.5f, 0.1f, WF_SINE } }
  },
  /* Noise Bell (slot 56, GM Cowbell) */
  {
    3, 357.0f, 1.0f, 0.0f,
    0.002f, 0.0f, 0.284f, 0.0f, 0.283f,
    0.5f, 0, 16000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.49f, WF_TRIANGLE },
      { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.01f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 3.25f, 0.0f, 0.0f, 0.05f, WF_SQUARE },
      { 1.0f, 0.0f, 0.0f, 0.05f, WF_SQUARE } }
  },
  /* Cymbal (slot 57, GM Crash Cymbal 2) */
  {
    11, 1509.0f, 1.0f, -0.17f,
    0.0f, 0.01f, 8.0f, 0.0f, 8.0f,
    0.5f, 1, 4125.0f, 0.01f, 1.0f,
    { { 1.04f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.06f, -4.3f, 5.5f, 0.03f, WF_SINE },
      { 2.38f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.18f, 0.0f, 5.7f, 0.03f, WF_SINE },
      { 2.39f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.28f, 0.0f, 6.1f, 0.03f, WF_SINE } }
  },
  /* Tight Clap (slot 58, GM Vibraslap) */
  {
    14, 1373.0f, 1.0f, 0.0f,
    0.023f, 0.005f, 2.798f, 0.0f, 2.798f,
    0.5f, 1, 1377.0f, 0.86f, 0.58f,
    { { 1.0f, 0.0f, 10.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.02f, -5.0f, 0.0f, 1.0f, WF_SAW },
      { 2.2f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 1.13f, 0.0f, 9.0f, 0.55f, WF_SINE } }
  },
  /* Tick Click (slot 59, GM Ride Cymbal 2) */
  {
    3, 776.0f, 0.58f, 0.0f,
    0.001f, 0.0f, 3.665f, 0.0f, 3.725f,
    0.5f, 1, 1873.0f, 0.01f, 1.0f,
    { { 1.02f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 1.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.11f, 0.0f, 2.5f, 0.05f, WF_SINE },
      { 1.87f, 0.0f, 3.5f, 0.05f, WF_SINE } }
  },
  /* Bongo (slot 60, GM Hi Bongo) */
  {
    2, 211.0f, 1.42f, 0.23f,
    0.005f, 0.01f, 0.4f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 3.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Bongo (slot 61, GM Low Bongo) */
  {
    2, 135.0f, 1.46f, 0.22f,
    0.01f, 0.0f, 0.356f, 0.0f, 0.551f,
    0.5f, 0, 16000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.481f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.109f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 2.49f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.397f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Bongo (slot 62, GM Mute Hi Conga) */
  {
    2, 211.0f, 1.6f, 0.23f,
    0.001f, 0.0f, 0.07f, 0.0f, 0.07f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 3.0f, 0.8f, WF_SINE },
      { 0.98f, 0.0f, 1.9f, 0.06f, WF_TRIANGLE } }
  },
  /* Bongo (slot 63, GM Open Hi Conga) */
  {
    2, 211.0f, 1.42f, 0.23f,
    0.005f, 0.01f, 0.4f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 3.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Bongo (slot 64, GM Low Conga) */
  {
    2, 127.0f, 1.42f, 0.23f,
    0.005f, 0.01f, 0.4f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 3.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Bongo (slot 65, GM High Timbale) */
  {
    2, 211.0f, 1.42f, 0.23f,
    0.005f, 0.01f, 0.4f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 3.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Bongo (slot 66, GM Low Timbale) */
  {
    2, 211.0f, 1.42f, 0.23f,
    0.005f, 0.01f, 0.4f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 3.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Snare Noise (slot 67, GM High Agogo) */
  {
    2, 472.0f, 1.0f, 0.0f,
    0.1f, 0.126f, 0.35f, 0.0f, 0.466f,
    0.5f, 0, 1425.0f, 1.0f, 0.56f,
    { { 1.0f, 0.0f, 0.9f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 1.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.13f, WF_SAW } }
  },
  /* Metal Stack (slot 68, GM Low Agogo) */
  {
    2, 359.0f, 1.0f, 0.0f,
    0.1f, 0.262f, 0.607f, 0.0f, 0.6f,
    0.5f, 0, 16000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.9f, 0.8f, WF_SINE },
      { 1.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 0.0f, -0.5f, 0.0f, 0.21f, WF_COSINE } }
  },
  /* Noise Bell (slot 69, GM Cabasa) */
  {
    7, 800.0f, 1.0f, 0.0f,
    0.002f, 0.01f, 0.6f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 1.0f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.0f, 0.8f, WF_SINE } }
  },
  /* Chime (slot 70, GM Maracas) */
  {
    2, 3102.0f, 1.2f, 0.0f,
    0.241f, 0.0f, 0.342f, 0.0f, 0.334f,
    0.5f, 1, 3173.0f, 0.0f, 0.78f,
    { { 1.0f, 0.0f, 7.5f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 4.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 3.54f, 0.0f, 5.1f, 0.07f, WF_SINE } }
  },
  /* Whistle (slot 71, GM Short Whistle) */
  {
    14, 2355.0f, 1.42f, 0.0f,
    0.102f, 0.03f, 0.15f, 0.0f, 0.15f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.96f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.03f, 4.1f, 3.2f, 1.0f, WF_COSINE },
      { 2.2f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 0.0f, 2.5f, 0.0f, 0.17f, WF_COSINE } }
  },
  /* Whistle (slot 72, GM Long Whistle) */
  {
    14, 2355.0f, 1.42f, 0.0f,
    0.102f, 0.2f, 0.15f, 0.0f, 0.15f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.96f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.03f, 4.1f, 3.2f, 1.0f, WF_COSINE },
      { 2.2f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 0.0f, 2.5f, 0.0f, 0.17f, WF_COSINE } }
  },
  /* Guiro (slot 73, GM Short Guiro) */
  {
    17, 660.0f, 1.0f, 0.0f,
    0.005f, 0.078f, 0.282f, 0.0f, 0.288f,
    0.5f, 0, 1245.0f, 0.0f, 1.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.4f, 0.0f, 0.0f, 0.01f, WF_SINE },
      { 0.11f, 0.0f, 0.5f, 1.0f, WF_SAW },
      { 0.0f, 1.1f, 0.0f, 0.1f, WF_SINE },
      { 1.79f, 0.0f, 0.0f, 0.41f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.01f, WF_SINE } }
  },
  /* Guiro (slot 74, GM Long Guiro) */
  {
    17, 660.0f, 1.0f, 0.0f,
    0.005f, 0.103f, 0.404f, 0.0f, 0.558f,
    0.5f, 0, 1245.0f, 0.0f, 1.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.4f, 0.0f, 0.0f, 0.01f, WF_SINE },
      { 0.05f, 0.0f, 0.5f, 1.0f, WF_SAW },
      { 0.05f, 0.4f, 0.0f, 0.03f, WF_SQUARE },
      { 1.79f, 0.0f, 0.0f, 0.45f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.01f, WF_SINE } }
  },
  /* Sub Kick (slot 75, GM Claves) */
  {
    0, 55.0f, 1.0f, 0.0f,
    0.001f, 0.01f, 0.3f, 0.0f, 0.2f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 0.67f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 3.0f, 0.8f, WF_SQUARE } }
  },
  /* Noise Clap (slot 76, GM Hi Wood Block) */
  {
    1, 650.0f, 1.0f, 0.0f,
    0.001f, 0.005f, 0.12f, 0.0f, 0.05f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.1f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 4.6f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.8f, 0.8f, WF_SINE } }
  },
  /* Closed Hat (slot 77, GM Low Wood Block) */
  {
    2, 3200.0f, 1.0f, 0.0f,
    0.001f, 0.005f, 0.12f, 0.0f, 0.06f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 4.5f, 0.8f, WF_SQUARE } }
  },
  /* Deep Tom (slot 78, GM Mute Cuica) */
  {
    3, 180.0f, 1.0f, 0.0f,
    0.002f, 0.01f, 0.4f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 0.8f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.2f, 0.8f, WF_SINE },
      { 1.5f, 0.0f, 1.2f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 1.5f, 0.8f, WF_SINE } }
  },
  /* Snare Body (slot 79, GM Open Cuica) */
  {
    4, 210.0f, 1.0f, 0.0f,
    0.002f, 0.01f, 0.25f, 0.0f, 0.25f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.5f, 0.0f, 1.8f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 4.0f, 0.8f, WF_SINE } }
  },
  /* Glass Bell (slot 80, GM Mute Triangle) */
  {
    3, 2420.0f, 0.6f, 0.0f,
    0.0f, 0.01f, 0.3f, 0.0f, 0.3f,
    0.5f, 0, 4125.0f, 0.01f, 1.0f,
    { { 1.04f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.43f, WF_SINE },
      { 2.38f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.0f, 3.7f, 0.0f, 0.3f, WF_TRIANGLE },
      { 1.0f, 2.2f, 0.0f, 0.19f, WF_SINE },
      { 1.43f, 0.0f, 0.0f, 0.09f, WF_COSINE } }
  },
  /* Glass Bell (slot 81, GM Open Triangle) */
  {
    3, 2420.0f, 0.6f, 0.0f,
    0.0f, 0.01f, 6.27f, 0.0f, 6.229f,
    0.5f, 0, 4125.0f, 0.01f, 1.0f,
    { { 1.04f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.43f, WF_SINE },
      { 2.38f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.0f, 3.7f, 0.0f, 0.3f, WF_TRIANGLE },
      { 1.0f, 2.2f, 0.0f, 0.19f, WF_SINE },
      { 1.43f, 0.0f, 0.0f, 0.09f, WF_COSINE } }
  },
  /* HighQ (slot 0) */
  {
    2, 10.0f, 2.0f, 0.0f,
    0.0f, 0.0f, 0.05f, 0.0f, 0.05f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.95f, WF_SINE },
      { 2.0f, 0.0f, 1.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 4.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.75f, WF_SAW } }
  },
  /* Snare Noise (slot 28) */
  {
    5, 200.0f, 1.0f, 0.0f,
    0.005f, 0.01f, 0.35f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 1.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 1.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Metal Stack (slot 29) */
  {
    6, 1200.0f, 1.0f, 0.0f,
    0.005f, 0.01f, 0.6f, 0.0f, 0.6f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 4.0f, 0.0f, 3.0f, 0.8f, WF_SINE } }
  },
  /* Twirl (slot 30) */
  {
    17, 1064.0f, 0.32f, 0.0f,
    1.303f, 0.554f, 1.33f, 0.5f, 1.947f,
    0.5f, 0, 1404.0f, 0.05f, 0.34f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 2.04f, 0.0f, 0.0f, 0.07f, WF_SINE },
      { 0.0f, 6.7f, 0.0f, 1.0f, WF_COSINE },
      { 0.0f, 0.3f, 0.0f, 0.21f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.03f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.04f, WF_SINE } }
  },
  /* Sub Kick (slot 32) */
  {
    3, 53.0f, 1.0f, 0.0f,
    0.0f, 0.013f, 0.953f, 0.0f, 0.93f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 0.0f, 0.0f, WF_SINE },
      { 0.01f, 0.0f, 0.0f, 0.03f, WF_SINE } }
  },
  /* SnareSlap (slot 34) */
  {
    6, 272.0f, 1.0f, 0.0f,
    0.005f, 0.01f, 0.566f, 0.0f, 0.569f,
    0.5f, 0, 4622.0f, 0.01f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_TRIANGLE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.0f, 0.0f, 10.0f, 0.02f, WF_SINE },
      { 1.23f, 0.0f, 0.1f, 0.03f, WF_SINE },
      { 0.81f, 0.0f, 0.0f, 0.03f, WF_SINE } }
  },
  /* Chime (slot 83) */
  {
    8, 1000.0f, 1.0f, 0.0f,
    0.002f, 0.01f, 1.0f, 0.0f, 0.9f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 4.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 5.0f, 0.0f, 3.0f, 0.8f, WF_SINE } }
  },
  /* Tight Clap (slot 84) */
  {
    9, 600.0f, 1.0f, 0.0f,
    0.001f, 0.005f, 0.15f, 0.0f, 0.12f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.1f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 1.8f, 0.8f, WF_SINE },
      { 2.2f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Tick Click (slot 85) */
  {
    10, 1500.0f, 1.0f, 0.0f,
    0.001f, 0.005f, 0.1f, 0.0f, 0.08f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 1.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 3.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 4.0f, 0.0f, 2.5f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 3.0f, 0.8f, WF_SINE } }
  },
  /* Glass FX (slot 86) */
  {
    11, 1700.0f, 1.0f, 0.0f,
    0.005f, 0.01f, 0.4f, 0.0f, 0.3f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.0f, 0.0f, 0.0f, 0.8f, WF_SINE },
      { 0.5f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 1.2f, 0.0f, 0.5f, 0.8f, WF_SINE },
      { 2.0f, 0.0f, 2.0f, 0.8f, WF_SINE },
      { 2.5f, 0.0f, 3.0f, 0.8f, WF_SINE },
      { 0.0f, 0.0f, 4.5f, 0.8f, WF_SINE } }
  },
  /* Rail bell (slot 87) */
  {
    10, 2000.0f, 1.0f, 0.0f,
    0.01f, 0.0f, 6.3f, 0.0f, 6.1f,
    0.5f, 0, 16000.0f, 0.5f, 0.0f,
    { { 1.047f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.481f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.109f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 2.49f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.397f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 2.49f, 0.0f, 0.0f, 0.8f, WF_SQUARE } }
  },
  /* Rail bell (slot 100) */
  {
    10, 2000.0f, 1.0f, 0.0f,
    0.01f, 0.0f, 6.3f, 0.0f, 6.1f,
    0.5f, 0, 20000.0f, 0.5f, 0.0f,
    { { 1.047f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.481f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.109f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 2.49f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 1.397f, 0.0f, 0.0f, 0.8f, WF_SQUARE },
      { 2.49f, 0.0f, 0.0f, 0.8f, WF_SQUARE } }
  },
};

static const char* const g_drum_inst_names[DRUM_INST_COUNT] = {
  "BassDrm", "Kick", "SideStk", "AccSnar", "HndClap", "SnBody1", "Tom1", "HiHat1",
  "Tom2", "HiHat2", "Tom3", "HiHat3", "Tom4", "Tom5", "Crash1", "Tom6", "ClHat1",
  "DpTom1", "SnBody2", "SnNois1", "Cymbal1", "NzBell1", "Cymbal2", "TgtClp1",
  "TickCl1", "Bongo1", "Bongo2", "Bongo3", "Bongo4", "Bongo5", "Bongo6", "Bongo7",
  "SnNois2", "MtlStk1", "NzBell2", "Chime1", "Whistl1", "Whistl2", "Guiro1",
  "Guiro2", "SubKck1", "NoisClp", "ClHat2", "DpTom2", "SnBody3", "GlasBl1", "GlasBl2",
  "HighQ", "SnNois3", "MtlStk2", "Twirl", "SubKck2", "SnrSlap", "Chime2", "TgtClp2",
  "TickCl2", "GlassFX", "RailBl1", "RailBl2",
};

static const uint8_t g_drum_inst_notes[DRUM_INST_COUNT] = {
  35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
  54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72,
  73, 74, 75, 76, 77, 78, 79, 80, 81, 0, 28, 29, 30, 32, 34, 83, 84, 85, 86,
  87, 100,
};

