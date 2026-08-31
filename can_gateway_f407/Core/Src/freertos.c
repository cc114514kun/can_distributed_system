/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_main.h"
#include <stdio.h>
#include "app_can_gw.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Stack overflow detected! LED1(PF10)常亮 + LED0(PF9)快闪 */
   (void)xTask;
   (void)pcTaskName;
   RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
   /* MODER: PF9=bit18-19, PF10=bit20-21, set to output(1) */
   GPIOF->MODER = (GPIOF->MODER & ~((3<<18)|(3<<20))) | ((1<<18)|(1<<20));
   GPIOF->OTYPER &= ~((1<<9)|(1<<10));
   GPIOF->BSRR = (1<<(10+16));  /* PF10 LOW (LED1) */
   /* No blocking print here: hook runs in task-switch context, calling
      printf->HAL_UART_Transmit would deadlock. LED pattern is the indicator. */
   NVIC_SystemReset();
   for(;;)
   {
       GPIOF->ODR ^= (1<<9);      /* Toggle PF9 (LED0�??) */
       for(volatile uint32_t i = 0; i < 200000; i++);
   }
}

/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* Malloc failed! LED0(PF9)常亮 + LED1(PF10)快闪 */
   RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
   GPIOF->MODER = (GPIOF->MODER & ~((3<<18)|(3<<20))) | ((1<<18)|(1<<20));
   GPIOF->OTYPER &= ~((1<<9)|(1<<10));
   GPIOF->BSRR = (1<<(9+16));   /* PF9 LOW (LED0) */
   /* No blocking print here: hook may run from ISR/task-switch context.
      HAL_UART_Transmit would deadlock; LED pattern is the indicator. */
   NVIC_SystemReset();
   for(;;)
   {
       GPIOF->ODR ^= (1<<10);     /* Toggle PF10 (LED1�??) */
       for(volatile uint32_t i = 0; i < 200000; i++);
   }
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
    /* TaskCanGateway ͳһ�� App_MainTask(app_main.c) �ڡ�bsp_can_init() ֮�󴴽���
       �����ظ���������ʵ����ͬһ���С��˴����ٴ����� */
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  extern void App_SystemInit(void);
  App_SystemInit();
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

