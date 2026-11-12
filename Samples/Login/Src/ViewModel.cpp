////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ViewModel.h"

#include <NsCore/ReflectionImplement.h>
#include <NsGui/PasswordBox.h>


using namespace Login;
using namespace Noesis;
using namespace NoesisApp;


////////////////////////////////////////////////////////////////////////////////////////////////////
ViewModel::ViewModel(): _loginSuccessful(false)
{
    StrCopy(_accountName, sizeof(_accountName), "");
    StrCopy(_message, sizeof(_message), "");
    _loginCommand.SetExecuteFunc(MakeDelegate(this, &ViewModel::Login));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::SetAccountName(const char* value)
{
    if (!StrEquals(_accountName, value))
    {
        StrCopy(_accountName, sizeof(_accountName), value);
        OnPropertyChanged("AccountName");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const char* ViewModel::GetAccountName() const
{
    return _accountName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::SetMessage(const char* value)
{
    if (!StrEquals(_message, value))
    {
        StrCopy(_message, sizeof(_message), value);
        OnPropertyChanged("Message");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const char* ViewModel::GetMessage() const
{
    return _message;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const DelegateCommand* ViewModel::GetLoginCommand() const
{
    return &_loginCommand;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool ViewModel::GetLoginSuccessful() const
{
    return _loginSuccessful;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::SetLoginSuccessful(bool value)
{
    if (_loginSuccessful != value)
    {
        _loginSuccessful = value;
        OnPropertyChanged("LoginSuccessful");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::Login(BaseComponent* param)
{
    CheckCredentials(static_cast<PasswordBox*>(param));

    if (_loginSuccessful)
    {
        // LOG IN!
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void ViewModel::CheckCredentials(PasswordBox* passwordBox)
{
    const char* password = passwordBox->GetPassword();

    if (StrIsNullOrEmpty(_accountName) && !StrIsNullOrEmpty(password))
    {
        SetMessage("ACCOUNT NAME CANNOT BE EMPTY");
        SetLoginSuccessful(false);
    }
    else if (!StrEquals(_accountName,"noesis") || !StrEquals(password, "12345"))
    {
        SetMessage("ACCOUNT NAME OR PASSWORD IS INCORRECT");
        SetLoginSuccessful(false);
    }
    else
    {
        SetMessage("LOGIN SUCCESSFUL");
        SetLoginSuccessful(true);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_BEGIN_COLD_REGION

NS_IMPLEMENT_REFLECTION(ViewModel)
{
    NsProp("AccountName", &ViewModel::GetAccountName, &ViewModel::SetAccountName);
    NsProp("Message", &ViewModel::GetMessage, &ViewModel::SetMessage);
    NsProp("LoginCommand", &ViewModel::GetLoginCommand);
    NsProp("LoginSuccessful", &ViewModel::GetLoginSuccessful, &ViewModel::SetLoginSuccessful);
}

NS_END_COLD_REGION
