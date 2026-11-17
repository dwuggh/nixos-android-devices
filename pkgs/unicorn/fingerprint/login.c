/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "login.h"
#include "protocol.h"
#include "tee.h"
#include <errno.h>
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>

struct credentials { const char *username, *password; };
static int converse(int count, const struct pam_message **messages,
                     struct pam_response **responses, void *context)
{
    struct credentials *c = context;
    if (count <= 0 || count > PAM_MAX_NUM_MSG || !messages || !responses)
        return PAM_CONV_ERR;
    struct pam_response *reply = calloc((size_t)count, sizeof(*reply));
    if (!reply)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; i++) {
        if (!messages[i])
            goto fail;
        switch (messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF: reply[i].resp = strdup(c->password); break;
        case PAM_PROMPT_ECHO_ON: reply[i].resp = strdup(c->username); break;
        case PAM_TEXT_INFO:
        case PAM_ERROR_MSG: continue;
        default: goto fail;
        }
        if (!reply[i].resp)
            goto fail;
    }
    *responses = reply;
    return PAM_SUCCESS;
fail:
    for (int i = 0; i < count; i++)
        if (reply[i].resp) {
            uf_wipe(reply[i].resp, strlen(reply[i].resp));
            free(reply[i].resp);
        }
    free(reply);
    return PAM_CONV_ERR;
}
int uf_login_authenticate(const char *username, const char *password)
{
    if (!username || !*username || !password || !*password) {
        errno = EINVAL;
        return -1;
    }
    struct credentials credentials = {username, password};
    struct pam_conv conversation = {converse, &credentials};
    pam_handle_t *handle = NULL;
    int status = pam_start("login", username, &conversation, &handle);
    if (status == PAM_SUCCESS)
        status = pam_set_item(handle, PAM_TTY, "/dev/tty");
    if (status == PAM_SUCCESS)
        status = pam_authenticate(handle, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
    if (status == PAM_SUCCESS)
        status = pam_acct_mgmt(handle, PAM_SILENT);
    int ended = handle ? pam_end(handle, status) : PAM_SUCCESS;
    if (status != PAM_SUCCESS || ended != PAM_SUCCESS) {
        errno = EACCES;
        return -1;
    }
    return 0;
}
