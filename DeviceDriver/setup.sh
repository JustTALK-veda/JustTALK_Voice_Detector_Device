#!/bin/bash

set -e

echo "||||||[0/5] dtc 의존성 확인||||||"
sudo apt update
#dtc 존재 여부 확인
if ! command -v dtc &> /dev/null; then
    echo "dtc(Device Tree Compiler)가 설치되어 있지 않아 설치합니다"
    sudo apt install -y device-tree-compiler
fi

echo "||||||[1/5] Device Tree Overlay 컴파일 및 복사 적용||||||"
#디바이스 트리 오버레이 생성
dtc -@ -I dts -O dtb -o dtoverlay/jstdev.dtbo dtoverlay/jstdev-overlay.dts
#디바이스 트리 자동 로드 설정
sudo cp dtoverlay/jstdev.dtbo /boot/firmware/overlays/
if ! grep -q "^dtoverlay=jstdev" /boot/firmware/config.txt; then
    echo "dtoverlay=jstdev" | sudo tee -a /boot/firmware/config.txt
fi


echo "||||||[2/5] Device Driver Module 빌드 및 설치||||||"
#드라이버 모듈 생성
make -C driver  
#모듈 자동 로드 설정
sudo cp driver/jstdev_module.ko /lib/modules/$(uname -r)/kernel/drivers/misc/
sudo depmod
echo "jstdev_module" | sudo tee /etc/modules-load.d/jstdev_module.conf > /dev/null
#노드 자동 권한 부여
echo 'KERNEL=="jstdev", MODE="0666"' | sudo tee /etc/udev/rules.d/99-jstdev.rules > /dev/null


echo "||||||[3/5] asound2 및 kissfft 설치||||||"
#kissfft 설치
sudo apt install -y libasound2-dev libkissfft-dev


echo "||||||[4/5] 사용자 앱 컴파일||||||"
gcc app/jstdev_app.c -o jstdev_app -lasound -lkissfft-float -lm


echo "||||||[5/5] 설치 완료, 재부팅 후 정상적으로 사용 가능합니다||||||"
echo "재부팅하시겠습니까? [y/N]"
read answer
if [[ "$answer" == "y" || "$answer" == "Y" ]]; then
    sudo reboot
fi