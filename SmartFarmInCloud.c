/*
 *      dht22.c:
 *	Simple test program to test the wiringPi functions
 *	Based on the existing dht11.c
 *	Amended by technion@lolware.net
 */

#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <mysql/mysql.h>

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <softPwm.h>

#include <time.h>
#include <math.h>

#include <pthread.h>

//#include "locking.h"

#define CS_MCP3208 8
#define SPI_CHANNEL 0
#define SPI_SPEED 1000000

#define VCC	4.8

#define RETRY 5
#define MAX 10000
#define MAXTIMINGS 85
#define PUMP 21

#define FAN 6 // 22
#define RGBLEDPOWER 24
#define RED 2 // 7
#define GREEN 3 // 8
#define BLUE 4 // 9

void sig_handler(int signo);
int ret_humid, ret_temp;

//static int DHTPIN = 7;
static int DHTPIN = 7;
static int dht22_dat[5] = {0,0,0,0,0};

//int read_dht22_dat();

#define DBHOST "115.68.228.55"
#define DBUSER "root"
#define DBPASS "wownsdnwnalstj"
#define DBNAME "Finalfarmdb"

MYSQL *connector;
MYSQL_RES *result;
MYSQL_ROW row;

pthread_mutex_t mutex;
pthread_cond_t fill, empty, fan, led;
int buffer[MAX];
int fill_ptr = 0;
int use_ptr = 0;
int count = 0;
int mon;

int read_mcp3208_adc(unsigned char adcChannel);
int read_dht22_dat();

int get_light_sensor() {
	unsigned char adcChannel_light = 0;
	int adcValue_light = 0;

	pinMode(CS_MCP3208, OUTPUT);
	adcValue_light = read_mcp3208_adc(adcChannel_light);
	return adcValue_light;
}

void put(int value) {	
	mon = fill_ptr;
	buffer[fill_ptr] = value;
	fill_ptr = (fill_ptr + 1) % MAX;
	count = count + 1;
}

int get() {
	int tmp = buffer[use_ptr];
	use_ptr = (use_ptr + 1) % MAX;
	count = count - 1;
	return tmp;
}

void *fan_thread(void *arg) {
	int i;
	for(i=0;i<MAX;i++) {
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&fan, &mutex);
		printf("FAN ON\n");
		digitalWrite(FAN, 1);
		pthread_mutex_unlock(&mutex);
		delay(5000);
		digitalWrite(FAN, 0);
		delay(1000);
	}
}

void *led_thread(void *arg) {
	int i;
	for(i=0;i<MAX;i++){
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&led, &mutex);
		printf("LED ON\n");
		softPwmWrite(RED, 153);
		softPwmWrite(GREEN, 217);
		softPwmWrite(BLUE, 234);
		pthread_mutex_unlock(&mutex);
	}
}

void *producer(void *arg) {
	int i;
	int temp, light;
	for(i=0;i<MAX;i++) {
		pthread_mutex_lock(&mutex);
		while(count == MAX)
			pthread_cond_wait(&empty, &mutex);
		
		temp = read_dht22_dat();
		light = get_light_sensor();
		//printf("temperature: %.2f ", temp);		
		//printf("lightness: %u\n", light);
	
		put(temp);
		printf("temperature: %d ", buffer[mon]);
		put(light);
		printf("lightness: %d\n", buffer[mon]);
		
		if (temp >= 27) {
			pthread_cond_signal(&fan);
		}

		if (light <= 900) {
			pthread_cond_signal(&led);
		}
		else {
			softPwmWrite(RED, 0);
			softPwmWrite(GREEN, 0);
			softPwmWrite(BLUE, 0);
		}

		pthread_cond_signal(&fill);
		pthread_mutex_unlock(&mutex);
		delay(1000);
	}
}

void *consumer(void *arg) {
	int i;
	for(i=0;i<MAX;i++) {
		pthread_mutex_lock(&mutex);
		while(count == 0)
			pthread_cond_wait(&fill, &mutex);

		int temp = get();
		int ligh = get();
				
		if(mon % 20 == 0 || mon % 20 == 1) {
			connector = mysql_init(NULL);
			if(!mysql_real_connect(connector, DBHOST, DBUSER, DBPASS, DBNAME, 3306, NULL, 0))
			{
				fprintf(stderr, "%s\n", mysql_error(connector));
				return 0;
			}

			printf("mysql opened\n");
			char query[1024];
		
			sprintf(query, "insert into SensorData values ( now(), %d, %d)", temp, ligh);
			if(mysql_query(connector, query))
			{
				fprintf(stderr, "%s\n", mysql_error(connector));
				printf("Write DB error\n");
			}
		}
		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&mutex);
	
	}
	
}

int read_mcp3208_adc(unsigned char adcChannel)
{
	unsigned char buff[3];
	int adcValue = 0;
	
	buff[0] = 0x06 | ((adcChannel & 0x07) >> 2);
	buff[1] = ((adcChannel & 0x07) << 6);
	buff[2] = 0x00;

	digitalWrite(CS_MCP3208, 0);
	
	wiringPiSPIDataRW(SPI_CHANNEL, buff, 3);
	
	buff[1] = 0x0f & buff[1];
	adcValue = (buff[1] << 8) | buff[2];

	digitalWrite(CS_MCP3208, 1);
	
	return adcValue;
}

static uint8_t sizecvt(const int read)
{
  /* digitalRead() and friends from wiringpi are defined as returning a value
  < 256. However, they are returned as int() types. This is a safety function */

  if (read > 255 || read < 0)
  {
    printf("Invalid data from wiringPi library\n");
    exit(EXIT_FAILURE);
  }
  return (uint8_t)read;
}

int read_dht22_dat()
{
  uint8_t laststate = HIGH;
  uint8_t counter = 0;
  uint8_t j = 0, i;

  dht22_dat[0] = dht22_dat[1] = dht22_dat[2] = dht22_dat[3] = dht22_dat[4] = 0;

  // pull pin down for 18 milliseconds
  pinMode(DHTPIN, OUTPUT);
  digitalWrite(DHTPIN, HIGH);
  delay(10);
  digitalWrite(DHTPIN, LOW);
  delay(18);
  // then pull it up for 40 microseconds
  digitalWrite(DHTPIN, HIGH);
  delayMicroseconds(40); 
  // prepare to read the pin
  pinMode(DHTPIN, INPUT);

  // detect change and read data
  for ( i=0; i< MAXTIMINGS; i++) {
    counter = 0;
    while (sizecvt(digitalRead(DHTPIN)) == laststate) {
      counter++;
      delayMicroseconds(1);
      if (counter == 255) {
        break;
      }
    }
    laststate = sizecvt(digitalRead(DHTPIN));

    if (counter == 255) break;

    // ignore first 3 transitions
    if ((i >= 4) && (i%2 == 0)) {
      // shove each bit into the storage bytes
      dht22_dat[j/8] <<= 1;
      if (counter > 50)
        dht22_dat[j/8] |= 1;
      j++;
    }
  }

  // check we read 40 bits (8bit x 5 ) + verify checksum in the last byte
  // print it out if data is good
  if ((j >= 40) && 
      (dht22_dat[4] == ((dht22_dat[0] + dht22_dat[1] + dht22_dat[2] + dht22_dat[3]) & 0xFF)) ) {
        float t, h;
		
        h = (float)dht22_dat[0] * 256 + (float)dht22_dat[1];
        h /= 10;
        t = (float)(dht22_dat[2] & 0x7F)* 256 + (float)dht22_dat[3];
        t /= 10.0;
        if ((dht22_dat[2] & 0x80) != 0)  t *= -1;
		
		ret_humid = (int)h;
		ret_temp = (int)t;
		//printf("Humidity = %.2f %% Temperature = %.2f *C \n", h, t );
		//printf("Humidity = %d Temperature = %d\n", ret_humid, ret_temp);
		
    return ret_temp;
  }
  else
  {
    printf("Data not good, skip\n");
    return 0;
  }
}

int main (void)
{
	signal(SIGINT, (void *)sig_handler);
	int adcChannel = 0;
	int adcValue[8] = {0};

	if(wiringPiSetup() == -1)
		exit(EXIT_FAILURE);

	if (wiringPiSetupGpio() == -1)
	{
		fprintf(stdout, "Unable to start wiringPi: %s\n", strerror(errno));
		return 1;
	}

	if (wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) == -1)
	{
		fprintf(stdout, "wiringPiSPISetup Failed: %s\n", strerror(errno));
		return 1;
	}

	if(setuid(getuid()) < 0) {
		perror("Dropping Failed\n");
		exit(EXIT_FAILURE);
	}

	
	pinMode(CS_MCP3208, OUTPUT);
	pinMode(FAN, OUTPUT);
	pinMode(RGBLEDPOWER, OUTPUT);
	pinMode(RED, OUTPUT);
	pinMode(GREEN, OUTPUT);
	pinMode(BLUE, OUTPUT);	
	
	softPwmCreate(RED, 0, 255);
	softPwmCreate(GREEN, 0, 255);
	softPwmCreate(BLUE, 0, 255);

	connector = mysql_init(NULL);
	if (!mysql_real_connect(connector, DBHOST, DBUSER, DBPASS, DBNAME, 3306, NULL, 0))
	{
		fprintf(stderr, "%s\n", mysql_error(connector));
		return 0;
	}
	
	pthread_t pro, con, fan_p, led_p;
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&empty, NULL);
	pthread_cond_init(&fill, NULL);
	pthread_cond_init(&fan, NULL);
	pthread_cond_init(&led, NULL);
	pthread_create(&con, NULL, consumer, NULL);
	pthread_create(&pro, NULL, producer, NULL);
	pthread_create(&fan_p, NULL, fan_thread, NULL);
	pthread_create(&led_p, NULL, led_thread, NULL);
	pthread_join(con, NULL);
	pthread_join(pro, NULL);
	pthread_join(fan_p, NULL);
	pthread_join(led_p, NULL);
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&fill);
	pthread_cond_destroy(&empty);
	pthread_cond_destroy(&fan);
	pthread_cond_destroy(&led);
	
	return 0;
}

void sig_handler(int signo)
{
	printf("process stop\n");
	digitalWrite(FAN, 0);
	softPwmWrite(RED, 0);
	softPwmWrite(GREEN, 0);
	softPwmWrite(BLUE, 0);
	exit(0);
}
