# DeviceDriver
Device Driver for STM32 Board Control

setup.sh : 한번만 실행하면 됨 
    재부팅마다 디바이스 트리와 드라이버 자동 설치 및 연결 진행


jstdev_app.c -> lib로 전환 예정(.so: 동적 라이브러리)


main 흐름

    fd = open("/dev/jstdev", O_RDONLY); 

    //read를 통해 디바이스에서 들어오는 신호를 저장하기 위한 버퍼
    uint8_t rx_buf[TOTAL_BYTES];    

    //마이크 0, 1, 2번 신호 데이터를 저장하기 위한 버퍼
    float ch0_buf[NUM_SAMPLES];
    float ch1_buf[NUM_SAMPLES];
    float ch2_buf[NUM_SAMPLES];

    //rx_buf에 신호 저장
    read(fd, rx_buf, TOTAL_BYTES)   

    //rx_buf의 데이터를 3개의 채널로 추출하여 저장
    extract_3channels(rx_buf, ch0_buf, ch1_buf, ch2_buf, NUM_SAMPLES);

    //해당 마이크로 들어온 신호가 음성(사람의 대화)신호인지 판단
    vad_detect(ch0_buf, NUM_SAMPLES, FS)


    //GCC-PHAT 연산을 통해 두 마이크 간 소리가 들어온 시간 차를 출력 (ch0_buf 기준)
    float t_01 = gcc_phat((float*)ch0_buf, (float*)ch1_buf, NUM_SAMPLES, 16000.0f, NULL);

    //3개의 마이크 간 각각의 시간 차를 입력받아 현재 신호의 방향 각도의 값을 확인 
    int angle = get_sound_direction(rx_buf, ch1_buf, ch2_buf, ch3_buf, NUM_SAMPLES, FS);
