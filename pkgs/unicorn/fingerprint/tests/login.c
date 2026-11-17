/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "login.h"
#include <assert.h>
#include <errno.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct pam_conv conversation;
static int auth_status, account_status, ended_status, authenticated, accounted, ended;
int pam_start(const char *service, const char *user, const struct pam_conv *conv, pam_handle_t **handle)
{
    assert(!strcmp(service, "login") && !strcmp(user, "test-user"));
    conversation = *conv;
    *handle = (pam_handle_t *)&conversation;
    return PAM_SUCCESS;
}
int pam_set_item(pam_handle_t *handle, int item, const void *value)
{
    assert(handle == (pam_handle_t *)&conversation && item == PAM_TTY && !strcmp(value, "/dev/tty"));
    return PAM_SUCCESS;
}
int pam_authenticate(pam_handle_t *handle, int flags)
{
    assert(handle == (pam_handle_t *)&conversation && flags == (PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK));
    authenticated++;
    struct pam_message password = {PAM_PROMPT_ECHO_OFF, "Password:"};
    const struct pam_message *messages[] = {&password};
    struct pam_response *reply = NULL;
    assert(conversation.conv(1, messages, &reply, conversation.appdata_ptr) == PAM_SUCCESS);
    assert(reply && !strcmp(reply[0].resp, "test-password"));
    memset(reply[0].resp, 0, strlen(reply[0].resp));
    free(reply[0].resp);
    free(reply);
    return auth_status;
}
int pam_acct_mgmt(pam_handle_t *handle, int flags)
{
    assert(handle == (pam_handle_t *)&conversation && flags == PAM_SILENT);
    accounted++;
    return account_status;
}
int pam_end(pam_handle_t *handle, int status)
{
    assert(handle == (pam_handle_t *)&conversation);
    ended++;
    ended_status = status;
    return PAM_SUCCESS;
}
int main(void)
{
    assert(!uf_login_authenticate("test-user", "test-password"));
    assert(authenticated == 1 && accounted == 1 && ended == 1 && !ended_status);
    auth_status = PAM_AUTH_ERR;
    assert(uf_login_authenticate("test-user", "test-password") == -1 && errno == EACCES);
    assert(authenticated == 2 && accounted == 1 && ended == 2 && ended_status == PAM_AUTH_ERR);
    auth_status = PAM_SUCCESS;
    account_status = PAM_ACCT_EXPIRED;
    assert(uf_login_authenticate("test-user", "test-password") == -1 && errno == EACCES);
    assert(accounted == 2 && ended == 3 && ended_status == PAM_ACCT_EXPIRED);
    assert(uf_login_authenticate("test-user", "") == -1 && errno == EINVAL);
    assert(authenticated == 3 && ended == 3);
    puts("login password: existing PAM auth, locked/expired account rejection and cleanup passed");
}
