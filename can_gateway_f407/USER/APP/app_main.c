#include "app_main.h"
#include "bsp_can.h"
#include "bsp_gpio.h"
#include "app_can_gw.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fault_mgr.h"
#include "app_uart_cli.h"
#include "app_sys_mon.h"
#include "app_config.h"
#include "app_sys_info.h"

/* Set to 1 to run bxCAN loopback self-test at startup.
 * Note: during loopback the controller may not ACK external frames,
 * which can cause the F103 sender to detect missing ACK and stop
 * transmitting. Leave 0 for normal F103->F407 data flow. */
#define CAN_LOOPBACK_TEST_AT_STARTUP 0U

TaskHandle_t AppMainTask_Handle = NULL;
extern UART_HandleTypeDef huart1;

static void App_MainTask(void *pvParameters)
{
    (void)pvParameters;

    /* Capture reset source + increment non-volatile boot counter early. */
    AppSysInfo_Init();

    BSP_GPIO_Init();
    AppCli_Init(&huart1);  /* create report queue + ReportTask BEFORE any App_ReportPrint */
    AppConfig_Load();
    App_NodeTable_Init();
    FaultMgr_Init();  /* FaultMgr_Init creates FaultMgrTask internally */

    if(bsp_can_init() != HAL_OK)
    {
        App_ReportPrint("[APP] CAN init FAILED!\r\n");
        for(;;)
        {
            BSP_Led_Toggle(LED_RUN_PIN);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    /* Startup logs removed: they flood the report queue and delay OK responses. */

    /* Create CAN gateway task (high priority for fast response). */
    xTaskCreate(TaskCanGateway, "CanGw", 1024, NULL, 4, NULL);

#if (CAN_LOOPBACK_TEST_AT_STARTUP != 0U)
    /* Loopback self-test. Keep disabled by default: during loopback the
     * F407 controller does not ACK frames from external nodes, so the F103
     * sender can enter error-passive / bus-off and stop transmitting. */
    {
        const uint8_t test_data[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
        bsp_can_loopback_test(0x12345678U, test_data, 5U);
    }
#endif

    bsp_can_self_diag();
    /* F407 TX test frame removed by default: if the bus has only one
     * listener (F103) and it is not ready to ACK, this frame can push the
     * F407 controller into Bus-Off / error-passive and break reception. */
#if 0
    {
        const uint8_t tx_data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
        HAL_StatusTypeDef tx_ret = bsp_can_send(0x18FF0001U, tx_data, 5U);
        App_ReportPrint("[APP] F407 TX test ID=0x18FF0001 => %s\r\n",
        (tx_ret == HAL_OK) ? "OK" : "FAIL");
    }
#endif

    /* Node monitor task (50ms period, detects offline nodes). */
    xTaskCreate(App_NodeMonitorTask, "App_NodeMonitorTask", 1024, NULL, 3, NULL);

    /* Debug print task disabled: [DATA] log every 500ms floods the report
       queue and delays CLI OK responses to the PC host. Sensor data is
       already visible on the host via GET_SENSOR polling. */
#if 0
    xTaskCreate(App_DebugPrintTask, "Debug", 1536, NULL, 2, NULL);
#endif

    /* System monitor + IWDG feed. Must run so IWDG is fed while healthy. */
    AppSysMon_Init();

    /* 500ms heartbeat: toggle RUN-LED to indicate system alive. */
    for(;;)
    {
        BSP_Led_Toggle(LED_RUN_PIN);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* Node 0 data print task, for debug. */
void App_DebugPrintTask(void *arg)
{
    (void)arg;
    SensorNode_t *p = &nodes[0];
    for(;;)
    {
        if(p->online)
        {
            App_ReportPrint("[DATA] V=%.2fV T=%.2fC K1=%d K2=%d alarm=%d\r\n",
            p->phys_data.voltage,
            p->phys_data.temperature,
            (p->data.key_state >> 0) & 1,
            (p->data.key_state >> 1) & 1,
            p->alarm);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void App_SystemInit(void)
{
    BaseType_t ret;
    ret = xTaskCreate(App_MainTask, "AppMain", 2048, NULL, 5, &AppMainTask_Handle);
    if(ret != pdPASS)
    {
        /* FreeRTOS task create failed; blink LED (cannot use vTaskDelay here). */
        for(;;)
        {
            BSP_Led_Toggle(LED_RUN_PIN);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}
