#pragma once

#include <security/pam_appl.h>

#include <boost/utility/string_view.hpp>
#include <cstring>
#include <memory>

// function used to get user input
inline int pamFunctionConversation(int numMsg, const struct pam_message** msg,
                                   struct pam_response** resp, void* appdataPtr)
{
    if (appdataPtr == nullptr)
    {
        return PAM_AUTH_ERR;
    }
    char* appPass = reinterpret_cast<char*>(appdataPtr);
    size_t appPassSize = std::strlen(appPass);
    char* pass = reinterpret_cast<char*>(malloc(appPassSize + 1));
    if (pass == nullptr)
    {
        return PAM_AUTH_ERR;
    }

    std::strcpy(pass, appPass);

    *resp = reinterpret_cast<pam_response*>(
        calloc(numMsg, sizeof(struct pam_response)));

    if (resp == nullptr)
    {
        return PAM_AUTH_ERR;
    }

    for (int i = 0; i < numMsg; ++i)
    {
        /* Ignore all PAM messages except prompting for hidden input */
        if (msg[i]->msg_style != PAM_PROMPT_ECHO_OFF)
        {
            continue;
        }

        /* Assume PAM is only prompting for the password as hidden input */
        resp[i]->resp = pass;
    }

    return PAM_SUCCESS;
}

inline bool pamAuthenticateUser(const std::string_view username,
                                const std::string_view password,
                                bool& passwordChangeRequired)
{
    std::string userStr(username);
    std::string passStr(password);
    passwordChangeRequired = false;
    const struct pam_conv localConversation = {
        pamFunctionConversation, const_cast<char*>(passStr.c_str())};
    pam_handle_t* localAuthHandle = NULL; // this gets set by pam_start

    if (pam_start("webserver", userStr.c_str(), &localConversation,
                  &localAuthHandle) != PAM_SUCCESS)
    {
        return false;
    }
    int retval = pam_authenticate(localAuthHandle,
                                  PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);

    if (retval != PAM_SUCCESS)
    {
        pam_end(localAuthHandle, PAM_SUCCESS);
        return false;
    }

    /* check if the account is healthy */
    switch (pam_acct_mgmt(localAuthHandle, PAM_DISALLOW_NULL_AUTHTOK))
    {
        case PAM_SUCCESS:
            break;
        case PAM_NEW_AUTHTOK_REQD:
            passwordChangeRequired = true;
            break;
        default:
            pam_end(localAuthHandle, PAM_SUCCESS);
            return false;
            break;
    }

    if (pam_end(localAuthHandle, PAM_SUCCESS) != PAM_SUCCESS)
    {
        return false;
    }

    return true;
}


/**
 * @brief Change the user's password
 *
 * @param[in] username Name of user account to change
 * @param[in] password The new password
 * @param[out] gotPamAuthtokError true, if pam_chauthtok returned PAM_AUTHTOK_ERR
 *             which usually means the password was rejected
 *
 * @returns true, if the password updated successfully */
inline bool pamUpdatePassword(const std::string& username,
                              const std::string& password,
                              bool& gotPamAuthtokError)
{
    gotPamAuthtokError = false;
    const struct pam_conv localConversation = {
        pamFunctionConversation, const_cast<char*>(password.c_str())};
    pam_handle_t* localAuthHandle = NULL; // this gets set by pam_start

    if (pam_start("passwd", username.c_str(), &localConversation,
                  &localAuthHandle) != PAM_SUCCESS)
    {
        return false;
    }
    int retval = pam_chauthtok(localAuthHandle, PAM_SILENT);

    if (retval != PAM_SUCCESS)
    {
        gotPamAuthtokError = (retval == PAM_AUTHTOK_ERR);
        pam_end(localAuthHandle, PAM_SUCCESS);
        return false;
    }

    if (pam_end(localAuthHandle, PAM_SUCCESS) != PAM_SUCCESS)
    {
        return false;
    }

    return true;
}
