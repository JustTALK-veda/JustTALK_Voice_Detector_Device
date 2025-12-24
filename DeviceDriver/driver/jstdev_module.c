//justTALK Device Driver
/*
 *  Control STM32 Board(NUCLEO_F401RE) with SPI & GPIO Pins
 * 
 * 
 */
#include <linux/init.h>
#include <linux/ioctl.h>        
#include <linux/fs.h>           /* Filesystem 커널 함수*/
#include <linux/device.h>
#include <linux/cdev.h> 		/* 문자 디바이스 */
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/printk.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <linux/gpio.h>
#include <linux/interrupt.h> 
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/atomic.h>

#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/uaccess.h>      /* copy_to_user( ), copy_from_user( ) 커널 함수 */

#define BCM_IO_BASE 0xFE000000 		/* Raspberry Pi 4의 I/O Peripherals 주소 */

#define GPIO_BASE (BCM_IO_BASE + 0x200000) 	/* GPIO 컨트롤러의 주소 */
#define GPIO_SIZE (256) 		/* 0x7E2000B0 - 0x7E2000000 + 4 = 176 + 4 = 180 */

/* GPIO 설정 매크로 */
#define GPIO_IN(g) (*(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))) 	/* 입력 설정 */
#define GPIO_OUT(g) (*(gpio+((g)/10)) |= (1<<(((g)%10)*3))) 	/* 출력 설정 */
#define GPIO_SET(g) (*(gpio+7) = 1<<g) 				/* 비트 설정 */
#define GPIO_CLR(g) (*(gpio+10) = 1<<g) 			/* 설정된 비트 해제 */
#define GPIO_GET(g) (*(gpio+13)&(1<<g)) 			/* 현재 GPIO의 비트에 대한 정보 획득 */
#define GPIO_FIX(g) (g+512)                         /* 버전에 의한 변경된 GPIO 번호 획득*/

/* 사용할 GPIO LISTS */
#define GPIO_SYNC GPIO_FIX(6)                       

#define DEVICE_MAJOR        200
#define DEVICE_MINOR        0

#define DEVICE_NAME "jstdev" 		/* 디바이스 디바이스 파일의 이름 */

#define TOTAL_BYTES 6144
#define HALF_BYTES 3072

/*	
*	STRUCT default variable name	
*
*	spi_data*		- jstdev_spi_data	(spidev)		//spi 디바이스의 데이터
*	class*			- jstdev_class		(spidev_class)		//디바이스 클래스
*	spi_device*		- jstdev			(spi)				//spi 디바이스
*	spi_driver		- jstdev_spi_driver	(spidev_spi_driver)	//디바이스 spi 드라이버
*
*/

struct spi_data {
	dev_t			devt;
	struct mutex		spi_lock;
	struct spi_device	*spi;
	struct list_head	device_entry;

	/* TX/RX buffers are NULL unless this device is open (users > 0) */
	struct mutex		buf_lock;
	unsigned		users;
	u8			*tx_buffer;
	u8			*rx_buffer;
	u32			speed_hz;

	/* MEMBER FOR INTERRUPT, WORKQUEUE, SENDSIG*/
	//int			irq_enabled; //users에 기능 통합 open한 task 있을 때만 인터럽트 활성화
	struct cdev cdev; 
	struct work_struct spi_work;
	wait_queue_head_t data_ready_queue;
	int data_ready;

};

static unsigned bufsize = 6144;
module_param(bufsize, uint, S_IRUGO);
MODULE_PARM_DESC(bufsize, "data bytes in biggest supported SPI message");

/* FUNCTIONS for FILE OPERATION */

static ssize_t jstdev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	printk(KERN_INFO "DEVICE READ!\n");

	struct spi_data *jstdev_spi_data = filp->private_data;
	ssize_t result = 0;

	if (!jstdev_spi_data || !jstdev_spi_data->rx_buffer)
	{
		return -EFAULT;
	}

	wait_event_interruptible(jstdev_spi_data->data_ready_queue, xchg(&jstdev_spi_data->data_ready, 0)); //원자적으로 바로 변경, 반환값은 기존값
	//jstdev_spi_data->data_ready = 0;	

	if (count > bufsize)
	{
		count = bufsize; 
	}
	
	mutex_lock(&jstdev_spi_data->buf_lock);

	/* 유저 공간으로 데이터 복사 */
	if (copy_to_user(buf, jstdev_spi_data->rx_buffer, count)) {
		result = -EFAULT;
	} else {
		result = count;
	}

	mutex_unlock(&jstdev_spi_data->buf_lock);

	//jstdev_spi_data->data_ready = 0;	


	return result;

}

static int jstdev_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "DEVICE OPEN!\n");

	struct spi_data *jstdev_spi_data;
	printk(KERN_INFO "OPEN A!\n");

	jstdev_spi_data = container_of(inode->i_cdev, struct spi_data, cdev);
	if(!jstdev_spi_data)
	{
		return -ENODEV;
	}

	// jstdev_spi_data = spi_get_drvdata(jstdev);
	// if(!jstdev_spi_data)
	// {
	// 	return -ENODEV;
	// }

	printk(KERN_INFO "OPEN D!\n");

	filp->private_data = jstdev_spi_data;
	printk(KERN_INFO "OPEN E!\n");

	//printk(KERN_INFO "OPEN F!\n");

	//jstdev_spi_data->irq_enabled = 1;

	if (!jstdev_spi_data->rx_buffer) {
		jstdev_spi_data->rx_buffer = kmalloc(bufsize, GFP_KERNEL);
		if (!jstdev_spi_data->rx_buffer)
		{
			return -ENOMEM;
		}
	}
	printk(KERN_INFO "OPEN G!\n");

	jstdev_spi_data->users++;
	printk(KERN_INFO "OPEN H!\n");

	return 0;

}

static int jstdev_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "DEVICE CLOSED!\n");

	struct spi_data *jstdev_spi_data = filp->private_data;

	if (jstdev_spi_data->users > 0)
		jstdev_spi_data->users--;

	/* 마지막 사용자일 경우 버퍼 정리 */
	if (jstdev_spi_data->users == 0) {
		kfree(jstdev_spi_data->rx_buffer);
		jstdev_spi_data->rx_buffer = NULL;

		//jstdev_spi_data->irq_enabled = 0;
	}

	return 0;
}

static const struct file_operations jstdev_fops = {
	.owner =	THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	// .write =	spidev_write,
	 .read =		jstdev_read,
	// .unlocked_ioctl = spidev_ioctl,
	// .compat_ioctl = spidev_compat_ioctl,
	 .open =		jstdev_open,
	 .release =		jstdev_release,
	// .llseek =	no_llseek,
};

/* 클래스(create) : /dev/의 노드를 만들기 위한 구조체 */
static struct class *jstdev_class; 

/* 드라이버 등록을 위한 구조체 배열 : of_match_table 및 id_table */
static const struct of_device_id jstdev_dt_id[] = {
    { .compatible = "jstdev" },
    { }
};
MODULE_DEVICE_TABLE(of, jstdev_dt_id);

static const struct spi_device_id jstdev_spi_ids[] = {
	{ "jstdev", 0 },
	{},
};
MODULE_DEVICE_TABLE(spi, jstdev_spi_ids);


/* workqueue : SPI 수신 및 완료 시 유저 알림*/
static void jstdev_spi_work(struct work_struct *work)
{
	printk(KERN_INFO "register WORKQUEUE!\n");

	struct spi_data *jstdev_spi_data = container_of(work, struct spi_data, spi_work);
	jstdev_spi_data->data_ready = 0;
	int result;

	struct spi_transfer tr[2] = {
		{
       		.rx_buf = jstdev_spi_data->rx_buffer,
        	.len = HALF_BYTES,		
        	.speed_hz = jstdev_spi_data->speed_hz,
    	},
		{
       		.rx_buf = jstdev_spi_data->rx_buffer + HALF_BYTES,
        	.len = HALF_BYTES,		
        	.speed_hz = jstdev_spi_data->speed_hz,
    	}
	};

	struct spi_message m;
	//spi_message_add_tail(&tr[1], &m);

    mutex_lock(&jstdev_spi_data->buf_lock);
	//mutex_lock(&jstdev_spi_data->spi_lock);
	spi_message_init(&m);			//spi_message 초기화
	spi_message_add_tail(&tr[0], &m);	//spi_transfer를 spi_message 끝에 추가
    result = spi_sync(jstdev_spi_data->spi, &m);	//spi_transfer들의 순차 실행
	spi_message_init(&m);			//spi_message 초기화
	spi_message_add_tail(&tr[1], &m);
	result = spi_sync(jstdev_spi_data->spi, &m);	//spi_transfer들의 순차 실행

	//mutex_unlock(&jstdev_spi_data->spi_lock);
    mutex_unlock(&jstdev_spi_data->buf_lock);
	if(result < 0)
	{
		pr_err("SPI transfer failed: %d\n", result);
		return;
	}

	jstdev_spi_data->data_ready = 1;
	wake_up(&jstdev_spi_data->data_ready_queue);
}

/* 인터럽트 서비스 루틴 : SPI 수신을 위한 워크큐 등록 실행 */
static irqreturn_t jstdev_sync_isr(int irq, void *dev)
{
	printk(KERN_INFO "INTERRUPUT SERVICE ROUTINE\n");

	struct spi_device *jstdev = dev;
	struct spi_data *jstdev_spi_data = spi_get_drvdata(jstdev);

	if(jstdev_spi_data->users == 0)		//////open 시 irq_enabled == 1로 설정 (장치 열었을때만 인터럽트 활성화)
		return IRQ_HANDLED;

	/* 워크큐 실행 */
	schedule_work(&jstdev_spi_data->spi_work);
    return IRQ_HANDLED;
}



/* PROBE & REMOVE */
static int jstdev_probe(struct spi_device *jstdev)		//bind 된 spi device
{
    printk(KERN_INFO "PROBE device!\n");

	int (*match)(struct device *dev);		//match라는 함수 포인터
	struct spi_data *jstdev_spi_data;		//spi device data 구조체
	int result;								//함수에 대한 결과값

	/* 파라미터로 얻은 spi device의 device 정보 */
	match = device_get_match_data(&jstdev->dev);	// can use spi_get_device_match_data(jstdev)
	if (match && match(&jstdev->dev)) 
	{
		return -ENODEV;
	}

	/* 디바이스 데이터 jstdev_spi_data 동적 할당 및 멤버 초기화 */
	jstdev_spi_data = kzalloc(sizeof(*jstdev_spi_data), GFP_KERNEL);
	if(!jstdev_spi_data)
	{
		return -ENOMEM;
	}

	jstdev_spi_data->spi = jstdev;

	mutex_init(&jstdev_spi_data->spi_lock);				//spi mutex 초기화
	mutex_init(&jstdev_spi_data->buf_lock);				//buf mutex 초기화

	INIT_LIST_HEAD(&jstdev_spi_data->device_entry);		//링크드 리스트의 헤드 초기화(사용 안함)
	

    INIT_WORK(&jstdev_spi_data->spi_work, jstdev_spi_work);

	init_waitqueue_head(&jstdev_spi_data->data_ready_queue);

	/* SPI 컨트롤러 설정 */
	jstdev->mode = SPI_MODE_0;
	jstdev->bits_per_word = (uint8_t)8;			
	jstdev->max_speed_hz = (uint32_t)10000000;	//자료형 맞춰주어야 SPI 정상작동
	result = spi_setup(jstdev);
	if(result < 0)
	{
		kfree(jstdev_spi_data);
		return result;
	}

	jstdev_spi_data->speed_hz = jstdev->max_speed_hz;

	/* /dev/ 노드 추가 */
	jstdev_spi_data->devt = MKDEV(DEVICE_MAJOR, DEVICE_MINOR);
	struct device *dev = device_create(jstdev_class, &jstdev->dev, jstdev_spi_data->devt, jstdev_spi_data, "jstdev");
	result = PTR_ERR_OR_ZERO(dev);
	if (result != 0)
	{
		kfree(jstdev_spi_data);				//디바이스 생성 실패 시, jstdev_spi_data 할당 반환
		return result;
	}

	spi_set_drvdata(jstdev, jstdev_spi_data);		//디바이스에 데이터 연결, spi_get_drvdata()로 구조체 가져오기 가능

	/* GPIO 초기화 */
	result = gpio_request(GPIO_SYNC, "sync_gpio");	//다른 곳에서 GPIO 핀을 사용하고 있는지 확인
	if(result != 0)
	{
		device_destroy(jstdev_class, jstdev_spi_data->devt);
		kfree(jstdev_spi_data);	
		return -EBUSY;
	}
	/* probe된 장치를 위한 cdev 등록 */
	cdev_init(&jstdev_spi_data->cdev, &jstdev_fops);
	jstdev_spi_data->cdev.owner = THIS_MODULE;
	result = cdev_add(&jstdev_spi_data->cdev, jstdev_spi_data->devt, 1);
	if (result < 0) 
	{
		pr_err("Failed to add cdev\n");
		device_destroy(jstdev_class, jstdev_spi_data->devt);
		kfree(jstdev_spi_data);
		return result;
	}
	result = gpio_direction_input(GPIO_SYNC);
	if (result != 0) {
        gpio_free(GPIO_SYNC);
        device_destroy(jstdev_class, jstdev_spi_data->devt);
        kfree(jstdev_spi_data);
        return result;
    }

	/* 인터럽트 추가 */
	jstdev->irq = gpio_to_irq(GPIO_SYNC);
	if (jstdev->irq < 0) 
	{
    	gpio_free(GPIO_SYNC);
    	device_destroy(jstdev_class, jstdev_spi_data->devt);
    	kfree(jstdev_spi_data);
    	return jstdev->irq;
	}

	result = request_irq(jstdev->irq, jstdev_sync_isr, IRQF_TRIGGER_RISING, "sync_gpio", jstdev);
	if(result != 0)
	{
		gpio_free(GPIO_SYNC);
		device_destroy(jstdev_class, jstdev_spi_data->devt);
        kfree(jstdev_spi_data);
		return result;
	}

	return 0;
}

static void jstdev_remove(struct spi_device *jstdev)
{
    printk(KERN_INFO "REMOVE device!\n");
	struct spi_data	*jstdev_spi_data = spi_get_drvdata(jstdev);

	mutex_lock(&jstdev_spi_data->spi_lock);	//중복 접근 방지
	jstdev_spi_data->spi = NULL;			//SPI data의 장치 초기화
	mutex_unlock(&jstdev_spi_data->spi_lock);

	free_irq(jstdev->irq, jstdev);
	gpio_free(GPIO_SYNC);

	cdev_del(&jstdev_spi_data->cdev);
	device_destroy(jstdev_class, jstdev_spi_data->devt);

	if (jstdev_spi_data->users == 0)		// 사용중인지 확인하는 코드
	{
		kfree(jstdev_spi_data);
	}	

}


/* INIT & EXIT*/
/* 
* DEVICE REGISTER & DRIVER REGISTER
* 캐릭터 디바이스 등록, 클래스 생성, SPI 드라이버 등록
* (디바이스는 dtbo를 통해 등록)
* 
*/
static struct spi_driver jstdev_spi_driver = {
	.driver = {
		.name =		"jstdev",
		.of_match_table = jstdev_dt_id,
	},
	.probe =	jstdev_probe,
	.remove =	jstdev_remove,
	.id_table =	jstdev_spi_ids,

	/* NOTE:  suspend/resume methods are not necessary here.
	 * We don't do anything except pass the requests to/from
	 * the underlying controller.  The refrigerator handles
	 * most issues; the controller driver handles the rest.
	 */
};

static int __init jstdev_init(void)
{
	printk(KERN_INFO "INIT module!\n");

	int result;		//함수의 실행 결과

	/* 200번 캐릭터 디바이스 등록, file operations 연결 */
	dev_t dev = MKDEV(DEVICE_MAJOR, DEVICE_MINOR);
	result = register_chrdev_region(dev, 1, DEVICE_NAME);
	if (result < 0) {
    	pr_err("Failed to register char device region: %d\n", result);
    	return result;
	}

	//result = register_chrdev(DEVICE_MAJOR, "jstdev", &jstdev_fops);
	// if (result < 0)
	// {
	// 	return result;
	// }

	/* 클래스 생성 */
	jstdev_class = class_create("jstdev");
	if (IS_ERR(jstdev_class)) 
	{
		unregister_chrdev(DEVICE_MAJOR, "jstdev");
		return PTR_ERR(jstdev_class);
	}

	/* spi 드라이버 등록 - 디바이스와 비교하여 맞는 디바이스와 연결 - 성공 시 probe 호출 */
	result = spi_register_driver(&jstdev_spi_driver);
	if (result < 0) 
	{
		class_destroy(jstdev_class);
		unregister_chrdev(DEVICE_MAJOR, "jstdev");
		return result;
	}

	return result;
}
module_init(jstdev_init);


static void __exit jstdev_exit(void)
{
    printk(KERN_INFO "EXIT module!\n");

	/* spi 드라이버 해제 - remove 호출 */
	spi_unregister_driver(&jstdev_spi_driver);
	class_destroy(jstdev_class);
	dev_t dev = MKDEV(DEVICE_MAJOR, DEVICE_MINOR);
	unregister_chrdev_region(dev, 1);
}
module_exit(jstdev_exit);

MODULE_AUTHOR("EUNHO CHOO");
MODULE_DESCRIPTION("User mode STM32 device SPI & GPIO interface for justTALK");
MODULE_ALIAS("spi:jstdev");
MODULE_LICENSE("GPL");
