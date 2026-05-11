#include "main.h"
#include "i2c-lcd.h"
#include "cmsis_os.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

/* ================= HANDLE ================= */

ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;

/* ================= GPIO DEFINE ================= */

#define GAS HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1)

/* LOCK PC14 */
/* LOCK PA8 */
/* LOCK PC14 */
#define LOCK_1 HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_SET)
#define LOCK_0 HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET)

/* BUZZER PA8 - ACTIVE LOW */
/* BUZZER PA8 - ACTIVE HIGH */
#define BUZZ_1 HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET)    // HIGH = bật còi
#define BUZZ_0 HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET)  // LOW  = tắt còi

/* ================= ROW OUTPUT ================= */

#define H1_1 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET)
#define H1_0 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET)

#define H2_1 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET)
#define H2_0 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET)

#define H3_1 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET)
#define H3_0 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET)

#define H4_1 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET)
#define H4_0 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET)

/* ================= COLUMN INPUT ================= */

#define C1 HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12)
#define C2 HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13)
#define C3 HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14)
#define C4 HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15)

/* ================= RTOS ================= */

QueueHandle_t keypadQueue;
SemaphoreHandle_t lcdMutex;
TimerHandle_t alarmTimer;

/* ================= VARIABLE ================= */

int adc_value;
float Voltage;
float lm35;

int pass[4] = {1,2,3,4};
int input[4];
int idx = 0;
volatile uint8_t password_mode = 0;
/* ================= FUNCTION ================= */

void SystemClock_Config(void);

static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
volatile uint8_t btn_open_flag = 0;
/* ================= KEYPAD ================= */

int scan_key()
{
    int key = -1;

    /* ROW 1 */
    H1_0; H2_1; H3_1; H4_1;

    if(C1 == 0) key = 1;
    if(C2 == 0) key = 2;
    if(C3 == 0) key = 3;
    if(C4 == 0) key = 10;

    /* ROW 2 */
    H1_1; H2_0; H3_1; H4_1;

    if(C1 == 0) key = 4;
    if(C2 == 0) key = 5;
    if(C3 == 0) key = 6;
    if(C4 == 0) key = 11;

    /* ROW 3 */
    H1_1; H2_1; H3_0; H4_1;

    if(C1 == 0) key = 7;
    if(C2 == 0) key = 8;
    if(C3 == 0) key = 9;
    if(C4 == 0) key = 12;

    /* ROW 4 */
    H1_1; H2_1; H3_1; H4_0;

    if(C1 == 0) key = 14;     // *
    if(C2 == 0) key = 0;
    if(C3 == 0) key = 15;     // #
    if(C4 == 0) key = 16;

    return key;
}



/* ================= EXTI CALLBACK ================= */
/* PC15 = NUT MO CUA */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == GPIO_PIN_15)
    {
        btn_open_flag = 1;
    }
}

/* ================= TASK KEYPAD ================= */

void Task_Keypad(void *arg)
{
    int key;
    int oldkey = -1;

    while(1)
    {
        key = scan_key();

        if(key != -1 && key != oldkey)
        {
            xQueueSend(keypadQueue,
                       &key,
                       0);

            oldkey = key;
        }

        if(key == -1)
        {
            oldkey = -1;
        }

        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

/* ================= TASK CONTROL ================= */

void Task_Control(void *arg)
{
    int key;
    char star[20];

    while(1)
    {
        /* ===== MO CUA BANG NUT NHAN ===== */

    	if(btn_open_flag == 1)
    	{
    	    btn_open_flag = 0;

    	    LOCK_1;

    	    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    	    lcd_clear();
    	    lcd_put_cur(0,0);
    	    lcd_send_string("MO CUA");
    	    lcd_put_cur(1,0);
    	    lcd_send_string("DOOR OPEN");
    	    xSemaphoreGive(lcdMutex);

    	    vTaskDelay(pdMS_TO_TICKS(3000));

    	    LOCK_0;

    	    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    	    lcd_clear();

    	    int t_int = (int)lm35;
    	    int t_dec = (int)(lm35 * 10) % 10;
    	    sprintf(star, "%d.%dC", t_int, t_dec);

    	    lcd_put_cur(0, 0);
    	    lcd_send_string("TEMP: ");
    	    lcd_send_string(star);

    	    lcd_put_cur(1, 0);
    	    if(GAS == 0)
    	        lcd_send_string("GAS DETECTED");
    	    else
    	        lcd_send_string("GAS NORMAL");

    	    xSemaphoreGive(lcdMutex);

    	    continue;
    	}

        /* ===== DOC KEYPAD ===== */

        if(xQueueReceive(keypadQueue,
                         &key,
                         pdMS_TO_TICKS(50)))
        {
            /* ===== NHAP SO ===== */
            /* ===== NHAN PHIM 16 ===== */

            if(key == 16 && password_mode == 0)
            {
                password_mode = 1;

                idx = 0;

                memset(input,0,sizeof(input));

                xSemaphoreTake(lcdMutex, portMAX_DELAY);

                lcd_clear();

                lcd_put_cur(0,0);
                lcd_send_string("NHAP PASSWORD");

                lcd_put_cur(1,0);
                lcd_send_string("----");

                xSemaphoreGive(lcdMutex);

                continue;
            }
            else if(key == 16 && password_mode == 1)
            {
                password_mode = 0;
                idx = 0;
                memset(input, 0, sizeof(input));

                xSemaphoreTake(lcdMutex, portMAX_DELAY);
                lcd_clear();

                // ✅ Dùng int thay float
                int t_int = (int)lm35;
                int t_dec = (int)(lm35 * 10) % 10;
                sprintf(star, "%d.%dC", t_int, t_dec);

                lcd_put_cur(0, 0);
                lcd_send_string("TEMP: ");
                lcd_send_string(star);

                lcd_put_cur(1, 0);
                if(GAS == 0)
                    lcd_send_string("GAS DETECTED");
                else
                    lcd_send_string("GAS NORMAL");

                xSemaphoreGive(lcdMutex);
                continue;
            }
            if(password_mode && key >= 0 && key <= 9)
            {
                if(idx < 4)
                {
                    input[idx] = key;
                    idx++;
                }

                memset(star,0,sizeof(star));

                for(int i=0;i<idx;i++)
                {
                    star[i] = '*';
                }

                xSemaphoreTake(lcdMutex, portMAX_DELAY);

                lcd_clear();

                lcd_put_cur(0,0);
                lcd_send_string("NHAP PASSWORD");

                lcd_put_cur(1,0);
                lcd_send_string(star);

                xSemaphoreGive(lcdMutex);
            }

            /* ===== XOA ===== */

            else if(password_mode && key == 14)
            {
                idx = 0;

                memset(input,0,sizeof(input));

                xSemaphoreTake(lcdMutex, portMAX_DELAY);

                lcd_clear();

                lcd_put_cur(0,0);
                lcd_send_string("NHAP PASSWORD");

                lcd_put_cur(1,0);
                lcd_send_string("----");

                xSemaphoreGive(lcdMutex);
            }

            /* ===== ENTER ===== */

            else if(password_mode == 1 && key == 15)
            {
                xSemaphoreTake(lcdMutex, portMAX_DELAY);

                lcd_clear();

                if(idx == 4)
                {
                	if(memcmp(input,pass,sizeof(pass)) == 0)
                	{
                	    LOCK_1;

                	    lcd_put_cur(0,0);
                	    lcd_send_string("MO CUA");

                	    lcd_put_cur(1,0);
                	    lcd_send_string("DOOR OPEN");

                	    xSemaphoreGive(lcdMutex);

                	    vTaskDelay(pdMS_TO_TICKS(3000));

                	    LOCK_0;

                	    /* ===== VE MAN HINH CHINH ===== */

                	    password_mode = 0;

                	    idx = 0;

                	    memset(input,0,sizeof(input));

                	    xSemaphoreTake(lcdMutex, portMAX_DELAY);

                	    lcd_clear();

                	    lcd_put_cur(0,0);

                	    int t_int = (int)lm35;
                	    int t_dec = (int)(lm35 * 10) % 10;
                	    sprintf(star, "%d.%dC", t_int, t_dec);

                	    lcd_send_string("TEMP: ");
                	    lcd_send_string(star);

                	    lcd_put_cur(1,0);

                	    if(GAS == 0)
                	    {
                	        lcd_send_string("GAS DETECTED");
                	    }
                	    else
                	    {
                	        lcd_send_string("GAS NORMAL");
                	    }

                	    xSemaphoreGive(lcdMutex);

                	    continue;
                	}
                    else
                    {
                        BUZZ_1;

                        lcd_put_cur(0,0);
                        lcd_send_string("WRONG PASS");

                        lcd_put_cur(1,0);
                        lcd_send_string("TRY AGAIN");

                        xSemaphoreGive(lcdMutex);

                        xTimerStart(alarmTimer,0);

                        vTaskDelay(pdMS_TO_TICKS(2000));

                        xSemaphoreTake(lcdMutex, portMAX_DELAY);

                        lcd_clear();

                        lcd_put_cur(0,0);
                        lcd_send_string("NHAP PASSWORD");

                        lcd_put_cur(1,0);
                        lcd_send_string("----");

                        xSemaphoreGive(lcdMutex);
                    }
                }
                else
                {
                    lcd_put_cur(0,0);
                    lcd_send_string("PASSWORD");

                    lcd_put_cur(1,0);
                    lcd_send_string("MUST 4 DIGIT");

                    xSemaphoreGive(lcdMutex);

                    vTaskDelay(pdMS_TO_TICKS(1500));

                    xSemaphoreTake(lcdMutex, portMAX_DELAY);

                    lcd_clear();

                    lcd_put_cur(0,0);
                    lcd_send_string("NHAP PASSWORD");

                    lcd_put_cur(1,0);
                    lcd_send_string("----");

                    xSemaphoreGive(lcdMutex);
                }

                idx = 0;

                memset(input,0,sizeof(input));


            }
        }
    }
}
/* ================= TASK SENSOR ================= */


void Task_Sensor(void *arg)
{
    while(1)
    {
        HAL_ADC_Start(&hadc1);

        if(HAL_ADC_PollForConversion(&hadc1,100) == HAL_OK)
        {
            adc_value = HAL_ADC_GetValue(&hadc1);

            Voltage = (float)adc_value * (3.3f / 4095.0f);

            lm35 = Voltage * 100.0f;
        }

        HAL_ADC_Stop(&hadc1);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


/* ================= TASK LCD ================= */
void Task_LCD(void *arg)
{
    char buff[20];

    while(1)
    {
        if(password_mode == 0)
        {
            xSemaphoreTake(lcdMutex, portMAX_DELAY);

            lcd_clear();

            // ✅ Dùng int thay float
            int t_int = (int)lm35;
            int t_dec = (int)(lm35 * 10) % 10;
            sprintf(buff, "TEMP:%d.%dC", t_int, t_dec);

            lcd_put_cur(0, 0);
            lcd_send_string(buff);

            lcd_put_cur(1, 0);
            if(GAS == 0)
                lcd_send_string("GAS DETECTED");
            else
                lcd_send_string("GAS NORMAL");

            xSemaphoreGive(lcdMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
/* ================= TIMER CALLBACK ================= */

void AlarmTimerCallback(TimerHandle_t xTimer)
{
    BUZZ_0;
}

/* ================= TASK ALARM ================= */

void Task_Alarm(void *arg)
{
    while(1)
    {
        if(password_mode == 0)
        {
            if(lm35 > 40 || GAS == 0)
            {
                BUZZ_1;
            }
            else
            {
                BUZZ_0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
/* ================= MAIN ================= */

int main(void)
{
    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();

    MX_ADC1_Init();
    HAL_ADCEx_Calibration_Start(&hadc1);
    MX_I2C1_Init();
    HAL_Delay(200);
    lcd_init();

    lcd_clear();

    lcd_put_cur(0,0);
    lcd_send_string("SMART HOME");

    lcd_put_cur(1,0);
    lcd_send_string("NHOM 10");

    HAL_Delay(3000);


    password_mode = 0;
    keypadQueue = xQueueCreate(10,sizeof(int));



    lcdMutex = xSemaphoreCreateMutex();

    alarmTimer = xTimerCreate("alarm",
                              pdMS_TO_TICKS(10000),
                              pdFALSE,
                              0,
                              AlarmTimerCallback);

    xTaskCreate(Task_Keypad,
                "keypad",
                128,
                NULL,
                2,
                NULL);

    xTaskCreate(Task_Control,
                "control",
                256,
                NULL,
                2,
                NULL);

    xTaskCreate(Task_Sensor,
                "sensor",
                128,
                NULL,
                2,
                NULL);

    xTaskCreate(Task_LCD,
                "lcd",
                256,
                NULL,
                1,
                NULL);

    xTaskCreate(Task_Alarm,
                "alarm",
                256,
                NULL,
                1,
                NULL);

    vTaskStartScheduler();

    while (1)
    {

    }
}

/* ================= GPIO INIT ================= */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* ROW OUTPUT */

    GPIO_InitStruct.Pin = GPIO_PIN_8 |
                          GPIO_PIN_9 |
                          GPIO_PIN_10 |
                          GPIO_PIN_11;

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* BUZZ + LOCK */

    /* LOCK PA8 */

    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* BUZZER PC14 */

    GPIO_InitStruct.Pin = GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    /* I2C PB6 PB7 */

    GPIO_InitStruct.Pin = GPIO_PIN_6 |
                          GPIO_PIN_7;

    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* COLUMN INPUT */

    GPIO_InitStruct.Pin = GPIO_PIN_12 |
                          GPIO_PIN_13 |
                          GPIO_PIN_14 |
                          GPIO_PIN_15;

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* LM35 PA0 */

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);


    /* GAS SENSOR */

    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PC15 BUTTON */

    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn,5,0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    /* DEFAULT */

    LOCK_0;
    BUZZ_0;

    H1_1;
    H2_1;
    H3_1;
    H4_1;
}

/* ================= ADC INIT ================= */

static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance = ADC1;

    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;

    HAL_ADC_Init(&hadc1);

    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

/* ================= I2C INIT ================= */

static void MX_I2C1_Init(void)
{
    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance = I2C1;

    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&hi2c1);
}

/* ================= CLOCK ================= */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK
                                | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1
                                | RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    HAL_RCC_ClockConfig(&RCC_ClkInitStruct,
                        FLASH_LATENCY_2);
}




