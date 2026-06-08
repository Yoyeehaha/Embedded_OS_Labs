/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "myprintf.h"
#include "string.h"
#include "waveplayer.h"
#include "File_Handling.h"
#include "semphr.h"
#include "queue.h"
#include "stdarg.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
	PLAYBACK_CONTROL,
	TRACK_SWITCHING,
	VOLUME_ADJUST
} SystemState;

typedef enum {
	NO_PRESS,
	SINGLE_PRESS,
	LONG_PRESS,
	DOUBLE_PRESS
} eButtonEvent;

typedef struct {
	char message[128];
} LogMessage;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AUDIO_EXIT_NOTIFY	(1UL << 0)
#define AUDIO_EXITED_NOTIFY (1UL << 1)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_spi3_tx;

SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
extern AUDIO_PLAYBACK_StateTypeDef AudioState;
TaskHandle_t buttonTaskHandle;
bool initialized  = false;
SystemState currentState = PLAYBACK_CONTROL;
bool single_press = false;
SemaphoreHandle_t fatfsMutex;
QueueHandle_t logQueue;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2S3_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void LogOperation(const char *format, ...){
	LogMessage msg;
	va_list args;
	va_start(args, format);
	vsnprintf(msg.message, sizeof(msg.message), format, args);
	va_end(args);
	if (xQueueSend(logQueue, &msg, pdMS_TO_TICKS(100)) != pdPASS){
		myprintf("Failed to send log message!\r\n");
	}
}

void PrintLogFile(void) {
	FIL logFile;
	FRESULT fr;
	UINT br;
	char buffer[128];
	xSemaphoreTake(fatfsMutex, portMAX_DELAY);
	//Write your code here to open the log.txt file for read.
	fr = f_open(&logFile, "log.txt", FA_READ);

	if (fr != FR_OK) {
		myprintf("Failed to open log.txt (Error: %d)\r\n", fr);
		xSemaphoreGive(fatfsMutex);
		return;
	}
	myprintf("Log file contents:\r\n");
	do {
		//Write your code here to read the log.txt file.
		fr = f_read(&logFile, buffer, sizeof(buffer) - 1, &br);

		if (fr != FR_OK) {
			myprintf("Error reading log.txt (Error: %d)\r\n", fr);
			break;
		}
		buffer[br] = '\0';
		myprintf("%s", buffer);
	} while (br == sizeof(buffer) - 1);
	//Write your code here to close the log.txt file.
	f_close(&logFile);

	xSemaphoreGive(fatfsMutex);
}
void LogTask(void *pvParameters) {
	FIL logFile;
	FRESULT fr;
	UINT bw;
	LogMessage logMsg;
	bool fileOpened = false;

	xSemaphoreTake(fatfsMutex, portMAX_DELAY);
	//Write your code here to open and clear the log.txt file. If the file is not exists, create a new one.
	fr = f_open(&logFile, "log.txt", FA_CREATE_ALWAYS);

	if (fr == FR_OK) {
		//Write your code here to close the log.txt file.
		f_close(&logFile);

		myprintf("Log file cleared.\r\n");
	} else {
		myprintf("Failed to clear log file, error = %d\r\n", fr);
	}
	xSemaphoreGive(fatfsMutex);

	for(;;){
		if (xQueueReceive(logQueue, &logMsg, portMAX_DELAY) == pdPASS) {
			xSemaphoreTake(fatfsMutex, portMAX_DELAY);
			if(!fileOpened) {
				//Write your code here to open the log.txt file for write.
				fr = f_open(&logFile, "log.txt", FA_OPEN_APPEND | FA_WRITE);

				if (fr != FR_OK){
					myprintf("LogTask: Failed to open log.txt, error = %d\r\n", fr);
					xSemaphoreGive(fatfsMutex);
					continue;
				}
				fileOpened = true;
			}

			//Write your code here to write to the log.txt file.
			fr = f_write(&logFile, logMsg.message, strlen(logMsg.message), &bw);

			if (fr == FR_OK){
				f_sync(&logFile);
				myprintf("LogTask: Wrote log entry.\r\n");

				//Write your code here to close the log.txt file.
				f_close(&logFile);

				fileOpened = false;
				xSemaphoreGive(fatfsMutex);
				PrintLogFile();
			} else {
				myprintf("LogTask: f_write error: %d\r\n", fr);
				xSemaphoreGive(fatfsMutex);
			}
		}
	}
}

bool buttonState() {
   static const uint32_t DEBOUNCE_MILLIS = 50;
   static bool buttonstate = false;
   static uint32_t buttonstate_ts = 0;

   uint32_t now = HAL_GetTick();

   if (!initialized) {
       buttonstate = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET;
       buttonstate_ts = now;
       initialized = true;
   }

   if (now - buttonstate_ts > DEBOUNCE_MILLIS) {
       bool current_state = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET);
       if (buttonstate != current_state) {
           buttonstate = current_state;
           buttonstate_ts = now;
       }
   }
   return buttonstate;
}

eButtonEvent getButtonEvent()
{
   static const uint32_t DOUBLE_GAP_MILLIS_MAX = 250;
   static const uint32_t LONG_MILLIS_MIN = 800;

   static uint32_t button_down_ts = 0;
   static uint32_t button_up_ts = 0;
   static bool double_pending = false;
   static bool long_press_pending = false;
   static bool button_down = false;
   static bool long_press_fired = false;

   static eButtonEvent pending_event = NO_PRESS;
   uint32_t now = HAL_GetTick();
   bool curDown = buttonState();

   /*write your code here*/
   /*Determine whether it's a single press, double press, or long press.*/
   if (curDown && !button_down) {
	   button_down = true;
	   button_down_ts = now;
	   long_press_pending = true;
   }

   if (!curDown && button_down) {
       button_down        = false;
       long_press_pending = false;

       if (!long_press_fired) {
           if (double_pending && (now - button_up_ts) <= DOUBLE_GAP_MILLIS_MAX) {
               /* 第二次放開且在間隔內 → Double Press */
               pending_event  = DOUBLE_PRESS;
               double_pending = false;
           } else {
               /* 第一次放開，等待看是否有第二次 */
               double_pending = true;
               button_up_ts   = now;
           }
       }
       long_press_fired = false;
   }


   if (curDown && long_press_pending && !long_press_fired) {
	   if ((now - button_down_ts) >= LONG_MILLIS_MIN) {
		   pending_event      = LONG_PRESS;
		   long_press_fired   = true;
		   long_press_pending = false;
		   double_pending     = false;
	   }
   }

   if (double_pending && !button_down && (now - button_up_ts) > DOUBLE_GAP_MILLIS_MAX) {
       pending_event  = SINGLE_PRESS;
       double_pending = false;
   }

   eButtonEvent event_to_return = pending_event;
   pending_event = NO_PRESS;
   return event_to_return;
}

void ButtonTask(void *pvParameters){
	while (1){
		xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(20));

		eButtonEvent event = getButtonEvent();
		if (event != NO_PRESS) {
			switch (currentState) {
				case PLAYBACK_CONTROL:
					if (event == SINGLE_PRESS) {
						myprintf("PLAYBACK_CONTROL: Single Press Detected. Toggling Play/Pause.\r\n");

						if (AudioState == AUDIO_STATE_PLAY){
							LogOperation("AUDIO_PAUSE\r\n");
							//Write your code here
							AudioState = AUDIO_STATE_PAUSE; // 短按一下暫停
						}
						if (AudioState == AUDIO_STATE_WAIT){
							LogOperation("AUDIO_RESUME\r\n");
							//Write your code here
							AudioState = AUDIO_STATE_RESUME; // 短按一下繼續撥放
						}
					}
					else if (event == DOUBLE_PRESS) {
						myprintf("PLAYBACK_CONTROL: Double Press Detected. Entering Track Switching Mode.\r\n");
						//Write your code here
						currentState = TRACK_SWITCHING; // 雙擊切換到換曲模式
					}
					else if (event == LONG_PRESS){
						myprintf("PLAYBACK_CONTROL: Long Press Detected. Entering Volume Adjust Mode.\r\n");
						//Write your code here
						currentState = VOLUME_ADJUST; // 長按切換到音量調節模式
					}
					break;
				case TRACK_SWITCHING:
					if (event == SINGLE_PRESS) {
						myprintf("TRACK_SWITCHING: Single Press Detected. Previous Track.\r\n");
						LogOperation("PREVIOUS_SONG\r\n");
						//Write your code here
						AudioState = AUDIO_STATE_PREVIOUS; // 短按切上一首
					}
					else if(event == DOUBLE_PRESS) {
						myprintf("TRACK_SWITCHING: Double Press Detected. Next Track.\r\n");
						LogOperation("NEXT_SONG\r\n");
						//Write your code here
						AudioState = AUDIO_STATE_NEXT; //雙擊切下一首
					}
					else if (event == LONG_PRESS){
						myprintf("TRACK_SWITCHING: Long Press Detected. Exiting Track Switching Mode.\r\n");
						//Write your code here
						currentState = PLAYBACK_CONTROL; // 長按切回撥放模式
					}
					break;
				case VOLUME_ADJUST:
					if(event == SINGLE_PRESS) {
						myprintf("VOLUME_ADJUST: Single Press Detected. Volume Down.\r\n");
						LogOperation("VOLUMN_DOWN\r\n");
						//Write your code here
						AudioState = AUDIO_STATE_VOLUME_DOWN; // 短按調低音量
					}
					else if(event == DOUBLE_PRESS) {
						myprintf("VOLUME_ADJUST: Double Press Detected. Volume Up.\r\n");
						LogOperation("VOLUMN_UP\r\n");
						//Write your code here
						AudioState = AUDIO_STATE_VOLUME_UP; // 長按調高音量
					}
					else if(event == LONG_PRESS){
						myprintf("VOLUME_ADJUST: Long Press Detected. Exiting Volume Adjust Mode.\r\n");
						//Write your code here
						currentState = PLAYBACK_CONTROL; // 長按切回撥放模式
					}
					break;
				default:
					break;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void AudioPlayerTask(void *pvParameters){
	int IsFinished = 0;
	xSemaphoreTake(fatfsMutex, portMAX_DELAY);
	AUDIO_PLAYER_Start(0);
	xSemaphoreGive(fatfsMutex);
	while (!IsFinished){
		xSemaphoreTake(fatfsMutex, portMAX_DELAY);
		AUDIO_PLAYER_Process(true);
		xSemaphoreGive(fatfsMutex);

		if (AudioState == AUDIO_STATE_STOP)
		{
			IsFinished = 1;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == GPIO_PIN_0){
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		vTaskNotifyGiveFromISR(buttonTaskHandle, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI2_Init();
  MX_USART2_UART_Init();
  MX_FATFS_Init();
  MX_I2C1_Init();
  MX_I2S3_Init();
  /* USER CODE BEGIN 2 */
  Mount_SD();
  fatfsMutex = xSemaphoreCreateMutex();
  if (fatfsMutex == NULL){
  	myprintf("Failed to create fatfsMutex!\r\n");
  }
  logQueue = xQueueCreate(10, sizeof(LogMessage));
  if (logQueue == NULL){
  	myprintf("Failed to create logQueue!\r\n");
  }

  xTaskCreate(LogTask, "LogTask", 512, NULL, 3, NULL);
  xTaskCreate(ButtonTask, "ButtonTask", 256, NULL, 2, &buttonTaskHandle);
  xTaskCreate(AudioPlayerTask, "AudioPlayerTask", 512, NULL, 3, NULL);
  vTaskStartScheduler();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2S3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S3_Init(void)
{

  /* USER CODE BEGIN I2S3_Init 0 */

  /* USER CODE END I2S3_Init 0 */

  /* USER CODE BEGIN I2S3_Init 1 */

  /* USER CODE END I2S3_Init 1 */
  hi2s3.Instance = SPI3;
  hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_8K;
  hi2s3.Init.CPOL = I2S_CPOL_LOW;
  hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S3_Init 2 */

  /* USER CODE END I2S3_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
