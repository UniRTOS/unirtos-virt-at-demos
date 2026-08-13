/*****************************************************************/ /**
* @file virt_at_demo.c
* @brief virtual AT demo implementation
* @author kruskal.zhu@quectel.com
* @date 2025-09
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date    <th>Version <th>Author      <th>Description
* <tr><td>2025-09 <td>1.0     <td>kruskal.zhu <td>Initial version
* </table>
**********************************************************************/

#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"

#include "qcm_virt_at.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG                  LOG_TAG

#define VIRT_AT_DEMO_TASK_STACK_SIZE 4096

static qosa_sem_t  g_virt_at_sem = QOSA_NULL;
static qosa_bool_t g_virt_at_timeout = QOSA_TRUE;
static qosa_task_t g_virt_at_task = QOSA_NULL;

// Global buffer for splicing segmented AT responses
#define VIRT_AT_RESP_BUFF_SIZE 512
static char g_virt_at_resp_buff[VIRT_AT_RESP_BUFF_SIZE] = {0};
// Current length of data in the response buffer
static qosa_uint32_t g_resp_buff_len = 0;

/**
 * @brief Virtual AT command result callback function
 * This function acts as a callback processing function for the execution results of virtual AT commands.
 * Responsible for receiving and processing the results returned by virtual AT commands.
 *
 * @param[in] data  A pointer to the returned data
 * @param[in] size  The size of the returned data
 *
 */
void virt_at_result(const char *data, qosa_uint32_t size)
{
    char *recv_buff = QOSA_NULL;

    if (size == 0 || data == QOSA_NULL)
    {
        QLOGE("VAT: invalid response data");
        return;
    }

    // Check for buffer overflow to prevent memory corruption
    if (g_resp_buff_len + size >= VIRT_AT_RESP_BUFF_SIZE)
    {
        QLOGE("VAT: resp buff overflow (cur len: %d, add size: %d), reset buff", g_resp_buff_len, size);
        qosa_memset(g_virt_at_resp_buff, 0, VIRT_AT_RESP_BUFF_SIZE);
        g_resp_buff_len = 0;
        return;
    }

    // Append current response chunk to global buffer to form complete response
    qosa_memcpy(g_virt_at_resp_buff + g_resp_buff_len, data, size);
    g_resp_buff_len += size;

    // Check if response contains "OK" (command success)
    if (qosa_strstr(g_virt_at_resp_buff, "OK") != NULL)
    {
        g_virt_at_timeout = QOSA_FALSE;   // Clear timeout flag: response received
        qosa_sem_release(g_virt_at_sem);  // Release semaphore to wake waiting task
        QLOGE("VAT: found OK in resp, release sem (buff len: %d)", g_resp_buff_len);
        qosa_memset(g_virt_at_resp_buff, 0, VIRT_AT_RESP_BUFF_SIZE);
        g_resp_buff_len = 0;
        return;
    }

    // Check if response contains "ERROR" (command failure)
    if (qosa_strstr(g_virt_at_resp_buff, "ERROR") != NULL)
    {
        g_virt_at_timeout = QOSA_FALSE;
        qosa_sem_release(g_virt_at_sem);
        QLOGE("VAT: found ERROR in resp, release sem (buff len: %d)", g_resp_buff_len);
        qosa_memset(g_virt_at_resp_buff, 0, VIRT_AT_RESP_BUFF_SIZE);
        g_resp_buff_len = 0;
        return;
    }

    recv_buff = (char *)qosa_malloc(size + 1);
    if (QOSA_NULL == recv_buff)
    {
        return;
    }
    qosa_memset(recv_buff, 0x00, size + 1);
    qosa_memcpy(recv_buff, data, size);

    // Print the length of the data returned by the virtual AT
    QLOGD("VAT result len: %d", size);
    // Print the specifics returned by the virtual AT
    QLOGD("VAT <--: %s", recv_buff);

    qosa_free(recv_buff);
    recv_buff = QOSA_NULL;
}

/**
 * @brief Virtual AT task demonstration function
 *
 */
static void unir_virt_at_demo_task(void *ctx)
{
    int i = 0;
    // Define an array of AT commands, containing 3 preset AT commands
    char *virt_at_cmd[3] = {"ATI\r\n", "AT+QDBGCFG=\"memory\"\r\n", "at+qdbgcfg=\"oem\"\r\n"};

    // The mission sleeps for 10 seconds and waits for the system to stabilize
    qosa_task_sleep_sec(10);

    // Initialize the virtual AT module and register the result callback function virt_at_result
    qcm_virt_at_init(virt_at_result);

    // Send 3 preset AT commands in a loop
    for (i = 0; i < 3; i++)
    {
        g_virt_at_timeout = QOSA_TRUE;

        // Print the AT command that is about to be sent and its index
        QLOGD("VAT[%d] -->: %s", i, virt_at_cmd[i]);

        // Send an AT command with the command string and string length
        qcm_virt_at_send(virt_at_cmd[i], qosa_strlen(virt_at_cmd[i]));

        // Wait up to 15 seconds for each command, and continue to execute the next command after the timeout
        qosa_sem_wait(g_virt_at_sem, 15 * 1000);

        // No OK response was received, and the command execution timed out
        if (g_virt_at_timeout)
        {
            QLOGE("virt at timeout");
            break;
        }
    }

    // Delete semaphore resources
    qosa_sem_delete(g_virt_at_sem);
    g_virt_at_sem = QOSA_NULL;

    // Delete the current task
    g_virt_at_task = QOSA_NULL;
}

/**
 * @brief Initialize the virtual AT demo module
 *
 */
void unir_virt_at_demo_init(void)
{
    int err = 0;

    err = qosa_sem_create_ex(&g_virt_at_sem, 0, 1);
    if (err != QOSA_OK)
    {
        QLOGE("sem create error");
    }

    err = qosa_task_create(&g_virt_at_task, VIRT_AT_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "vat_demo", unir_virt_at_demo_task, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("task create error");
        return;
    }
}
UNIRTOS_APP_EXPORT(328, "virt_at_demo", unir_virt_at_demo_init);
