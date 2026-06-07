#include "ifft_q15.h"
#include <math.h>

static int16_t  tw_cos[FFT_N/2];
static int16_t  tw_sin[FFT_N/2];   // = -sin(2πk/N)
static uint16_t brev[FFT_N];

void fft_q15_init() {
    for (int k = 0; k < FFT_N/2; k++) {
        double a = 2.0 * M_PI * (double)k / (double)FFT_N;
        tw_cos[k] = (int16_t)lround( cos(a) * 32767.0);
        tw_sin[k] = (int16_t)lround(-sin(a) * 32767.0);
    }
    for (int i = 0; i < FFT_N; i++) {
        unsigned r = 0, x = i;
        for (int b = 0; b < FFT_LOG2; b++) { r = (r<<1)|(x&1); x >>= 1; }
        brev[i] = (uint16_t)r;
    }
}

// 前進FFT (int32データ, スケーリングなし)
static void fft_fwd(int32_t* re, int32_t* im) {
    for (int i = 0; i < FFT_N; i++) {
        int j = brev[i];
        if (j > i) {
            int32_t t;
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= FFT_N; len <<= 1) {
        int half = len >> 1;
        int step = FFT_N / len;
        for (int i = 0; i < FFT_N; i += len) {
            int ti = 0;
            for (int j = 0; j < half; j++) {
                int16_t wr = tw_cos[ti];
                int16_t wi = tw_sin[ti];
                ti += step;
                int a = i + j, b = a + half;
                int32_t xr = re[b], xi = im[b];
                int32_t tr = (int32_t)(((int64_t)wr*xr - (int64_t)wi*xi) >> 15);
                int32_t tii= (int32_t)(((int64_t)wr*xi + (int64_t)wi*xr) >> 15);
                int32_t ar = re[a], ai = im[a];
                re[b] = ar - tr;  im[b] = ai - tii;
                re[a] = ar + tr;  im[a] = ai + tii;
            }
        }
    }
}

// 逆FFT: ifft(X) = conj(fft(conj(X)))  ※1/Nスケールしない(出力をN倍に保つ)
// → 加算合成で適正な振幅を得るため
void ifft_q15(int32_t* re, int32_t* im) {
    for (int i = 0; i < FFT_N; i++) im[i] = -im[i];
    fft_fwd(re, im);
    for (int i = 0; i < FFT_N; i++) im[i] = -im[i];
    // re[] = N×ifft (時間信号)。最終スケールは呼び出し側のマスターゲインで調整
}