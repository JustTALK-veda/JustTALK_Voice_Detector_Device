#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>

#include <kissfft/kiss_fftr.h> //need -lkissfft-float for compile
#include <math.h> //need -lm for compile

#define SPI_DEV         "/dev/jstdev"
#define SPI_SPEED       10000000

#define NUM_SAMPLES     1024
#define NUM_CHANNELS    3

#define TOTAL_SAMPLES   (NUM_SAMPLES * NUM_CHANNELS)

#define TOTAL_BYTES     (TOTAL_SAMPLES * 2)

#define FS 24000.0f
#define MIC_DIST 0.10f
#define PI M_PI
#define SPEED_OF_SOUND 343.0f

#define VOICE_LOW 300.0f
#define VOICE_HIGH  3400.0f

#define FILTER_NUM 3

/* CHANNEL SPLIT FUNCTION : make [6144 bytes] to [16 bit(2 bytes) * 1024 samples * 3 channels] */
void extract_3channels(const uint8_t *rx_buf, float* ch0, float* ch1, float* ch2, int samples)
{
    for(int i = 0; i < samples; i++)
    {
        ch0[i] = (float)(((rx_buf[i*6 + 1] << 8) | rx_buf[i*6 + 0])-1551) / 1241.0f;
        ch1[i] = (float)(((rx_buf[i*6 + 3] << 8) | rx_buf[i*6 + 2])-1551) / 1241.0f;
        ch2[i] = (float)(((rx_buf[i*6 + 5] << 8) | rx_buf[i*6 + 4])-1551) / 1241.0f;
    }
}

/* SIGNAL PROCESSING FUCTIONS */

/* Apply Hanning Window */
void apply_window(float *signal, int n)
{
    for(int i = 0; i < n; i++)
    {
        float w = 0.5f - 0.5f * cosf(2.0f * PI *i / (n-1));
        signal[i] *= w;
    }
}

/* Voice Activity Detection */
int vad_detect(float *x, int n, float fs)
{
    float energy = 0.0f;
    float band_energy = 0.0f;
    float bin_res = fs / n;

    kiss_fftr_cfg fft_cfg = kiss_fftr_alloc(n, 0, NULL, NULL);
    kiss_fft_cpx X[n/2 + 1];
    kiss_fftr(fft_cfg, x, X);

    for(int i=0; i<=n/2;i++)
    {
        float freq = bin_res * i;
        float mag2 = X[i].r*X[i].r + X[i].i *X[i].i;
        if(freq >= VOICE_LOW && freq <= VOICE_HIGH) 
        {
            band_energy += mag2;
        }
        energy += mag2;
    }
    free(fft_cfg);
    return band_energy > 0.015f && (band_energy / energy) > 0.35f;
}


/* GCC PHAT FUNCTION */
float gcc_phat(float *x, float *y, int n, float fs)
{
    /* INIT */
    kiss_fftr_cfg fft_cfg = kiss_fftr_alloc(n, 0, NULL, NULL);
    kiss_fftr_cfg ifft_cfg = kiss_fftr_alloc(n, 1, NULL, NULL);

    kiss_fft_cpx X[n/2 + 1], Y[n/2 + 1], R[n/2 +1];
    float corr[n];

    /* FFT */
    kiss_fftr(fft_cfg, x, X);
    kiss_fftr(fft_cfg, y, Y);

    /* CROSS-SPECTRUM */
    int low_bin = (int)(VOICE_LOW * n / fs);
    int high_bin = (int)(VOICE_HIGH * n / fs);

    for (int i = 0; i < n / 2 + 1; i++) {
        if(i < low_bin || i > high_bin)
        {
            R[i].r = 0.0f;
            R[i].i = 0.0f;
            continue;
        }
        float real = X[i].r * Y[i].r + X[i].i * Y[i].i;
        float imag = X[i].i * Y[i].r - X[i].r * Y[i].i;
        float mag = sqrtf(real * real + imag * imag);
        if (mag < 1e-2f) 
        {
            mag = 1e-2f;
        }
        R[i].r = real / mag;
        R[i].i = imag / mag;
    }

    kiss_fftri(ifft_cfg, R, corr);
    free(fft_cfg);
    free(ifft_cfg);

    /* get Max Value of Cross-Correlation*/
    int max_idx = 0;
    float max_val = -1e10f;
    for (int i = 0; i < n; i++) 
    {
        if (corr[i] > max_val) 
        {
            max_val = corr[i];
            max_idx = i;
        }
    }

    /* 서브샘플 보간 (parabolic) */
    int idx = max_idx;
    float denom = 2 * (2 * corr[idx] - corr[(idx+1)%n] - corr[(idx+n-1)%n]);
    float offset = 0;
    if (fabs(denom) > 1e-8f)
        offset = (corr[(idx+n-1)%n] - corr[(idx+1)%n]) / denom;

    float lag = idx + offset;
    if (lag > n/2) lag -= n;
    return lag / fs;
}

int estimate_angle_custom(float t_ab, float t_bc, float t_ac, int mode)
{
    //하이브리드 추정 방법 적용
    int section = 0;
    float base_angle;
    float sub_angle = 30.0f;
    float tdoa;
    float a_val;
    float t_ca = -t_ac;

    if(t_ab > 0) section |= 1<<2;     //a보다 b가 더 가까움
    if(t_bc > 0) section |= 1<<1;     //b보다 c가 더 가까움
    if(t_ca > 0) section |= 1<<0;     //c보다 a가 더 가까움

    switch(section)
    {
        case 1: case 5:
            base_angle = 60.0f;
            tdoa = t_ab;
            break;
        case 4: case 6:
            base_angle = 180.0f;
            tdoa = t_bc;
            break;
        case 2: case 3: 
            base_angle = 300.0f;
            tdoa = t_ca;
            break;
        default:
            return -2;
    }

    a_val = (tdoa * SPEED_OF_SOUND / MIC_DIST);

    if(a_val<-0.98f)
    {
        sub_angle = -60.0f;
    }
    else if(a_val>0.98f)
    {
        sub_angle = 60.0f;
    }
    else if(a_val<0.20f && a_val>-0.20f)
    {
        sub_angle = 0.0f;
    }else{
        if(a_val<0)
        {
            sub_angle *= -1;
        }

        sub_angle = (-((acosf(a_val) * 180.0f / PI )-90.0f))*5.0f/6.0f;
    }

    //printf("| %3d |\n", (int)(base_angle + sub_angle));

    return (int)(base_angle + sub_angle);
}

/* get direction of sound from device */
int get_sound_direction(uint8_t *rx_buf, float* ch1, float* ch2, float* ch3, int num, int fs)
{
    //Pre-Processing
    extract_3channels(rx_buf, ch1, ch2, ch3, num);
        
    apply_window(ch1, num);  
    apply_window(ch2, num);   
    apply_window(ch3, num);

    if(vad_detect(ch1, num, fs)&&vad_detect(ch2, num, fs)&&vad_detect(ch3, num, fs))
    {
        float t_12 = gcc_phat((float*)ch1, (float*)ch2, num, fs);
        float t_23 = gcc_phat((float*)ch2, (float*)ch3, num, fs);
        float t_13 = gcc_phat((float*)ch1, (float*)ch3, num, fs);
        
        return estimate_angle_custom(t_12, t_23, t_13, 0);
    }
    return -1;
}

/* median filter */
int median_filter(int* arr, int n)
{
    int tmp[5];
    memcpy(tmp, arr, n*sizeof(int));

    for(int i = 1; i <n; i++)
    {
        int key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    return tmp[n / 2];  // 중앙값 반환
}



int main() {
    int fd = open("/dev/jstdev", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    /* Buffers */
    uint8_t rx_buf[TOTAL_BYTES];

    float ch1_buf[NUM_SAMPLES];
    float ch2_buf[NUM_SAMPLES];
    float ch3_buf[NUM_SAMPLES];

    int angle_arr[5];
    int angle_idx = 0;
    int angle_num = 0;
    int angle;
    int filtered_angle;
    while (1) {
        ssize_t len = read(fd, &rx_buf[0], TOTAL_BYTES);  // reading end of DATA
        if (len < 0) {
            perror("read");
            break;
        }
        angle = get_sound_direction(rx_buf, ch1_buf, ch2_buf, ch3_buf, NUM_SAMPLES, FS);
        if(angle>=0)
        {
            angle_arr[angle_idx] = angle;
            angle_idx = (angle_idx+1) % FILTER_NUM;

            if(angle_num < FILTER_NUM)
            {
                angle_num++;
            }
            if(angle_num == FILTER_NUM)
            {
                filtered_angle = median_filter(angle_arr, FILTER_NUM);
            }
            
            printf("| %3d |\n", filtered_angle);
            
        }


    }
    close(fd);
    return 0;
}
