# DeviceDriver
Device Driver for STM32 Board Control
<img width="339" height="272" alt="image" src="https://github.com/user-attachments/assets/2fbc38c9-ff2b-4f0f-868e-a82b1ff6a902" />

+ DeviceDriver : RaspberryPi 4에서의 디바이스 동작 설정을 위한 디렉토리
    + app : 드라이버 사용을 위한 예시 프로그램
        + jstdev_app.c
          ```      
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
                
            //3개의 마이크 간 각각의 시간 차를 입력받아 현재 신호의 방향 각도의 값을 확인 
            int angle = get_sound_direction(rx_buf, ch1_buf, ch2_buf, ch3_buf, NUM_SAMPLES, FS);
          ```
    + driver : 디바이스 드라이버의 소스코드
        + Makefile
        + jstdev_module.c 
    + dtoverlay : JustTALK 디바이스 등록을 위한 dts 오버레이 소스코드
        + jstdev-overlay.dts 
    + setup.sh
      ```
        한번 실행하여 driver, dtoverlay에 필요한 부분 설치 진행
        디바이스 트리 오버레이 컴파일 및 부팅 시 자동 로드 설정
        디바이스 드라이버 모듈 빌드 및 설치 (재부팅마다 디바이스 트리와 드라이버 자동 설치 및 연결 진행)
        유저 프로그램에 필요한 라이브러리 설치
        음성 방향 탐지 예제 프로그램(jstdev_app.c) 컴파일
      ```    
+ STM32 : STM32CubeIDE를 통해 STM32 NUCLEO F401RE 보드로 firmware를 build 및 run 실행을 위한 디렉토리


