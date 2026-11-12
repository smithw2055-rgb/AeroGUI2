module Login;

import Aero.UI;

[Observable]
public class ViewModel
{
    public string AccountName { get; set; } = "";
    public string Message { get; set; } = "";
    public bool LoginSuccessful { get; set; } = false;
    public int LoginAttempts { get; set; } = 0;

    public ViewModel()
    {
        this.AccountName = "";
        this.Message = "";
        this.LoginSuccessful = false;
        this.LoginAttempts = 0;
    }

    public bool ValidateLogin(string password)
    {
        this.LoginAttempts = this.LoginAttempts + 1;

        bool accountEmpty = (this.AccountName == null || this.AccountName == "");
        bool passwordEmpty = (password == null || password == "");

        if (accountEmpty && !passwordEmpty)
        {
            this.Message = "ACCOUNT NAME CANNOT BE EMPTY";
            this.LoginSuccessful = false;
            return false;
        }

        if (this.AccountName != "noesis" || password != "12345")
        {
            this.Message = "ACCOUNT NAME OR PASSWORD IS INCORRECT";
            this.LoginSuccessful = false;
            return false;
        }

        this.Message = "LOGIN SUCCESSFUL";
        this.LoginSuccessful = true;
        return true;
    }

    public bool CheckCredentials(string password)
    {
        return this.ValidateLogin(password);
    }

    public void Reset()
    {
        this.AccountName = "";
        this.Message = "";
        this.LoginSuccessful = false;
    }
}

[Observable]
public partial class MainWindow
{
    public ViewModel ViewModel { get; set; }
    private handle _emptyHandle;

    public MainWindow()
    {
        this.ViewModel = new ViewModel();
    }

    public void OnInitialized()
    {
    }

    public void Login(handle passBox)
    {
        if (this.ViewModel == null)
        {
            return;
        }

        handle target = passBox;
        if (target == this._emptyHandle)
        {
            target = this.PassBox;
        }

        string password = PasswordBox_GetPassword(target);
        bool ok = this.ViewModel.ValidateLogin(password);

        if (ok)
        {
            Storyboard_Begin(this.__nativeRootHandle, "ShowLoginMessage");
            Storyboard_Begin(this.__nativeRootHandle, "LoginSuccesful");
        }
        else
        {
            Storyboard_Begin(this.__nativeRootHandle, "ShowErrorMessage");
            if (this.ViewModel.AccountName == null || this.ViewModel.AccountName == "")
            {
                UIElement_Focus(this.UserBox);
            }
            else
            {
                UIElement_Focus(target);
            }
        }
    }

    public void SubmitLogin()
    {
        this.Login(this.PassBox);
    }

    public bool LoginWithPassword(string password)
    {
        if (this.ViewModel == null)
        {
            return false;
        }

        bool ok = this.ViewModel.ValidateLogin(password);
        if (ok)
        {
            Storyboard_Begin(this.__nativeRootHandle, "ShowLoginMessage");
            Storyboard_Begin(this.__nativeRootHandle, "LoginSuccesful");
        }
        else
        {
            Storyboard_Begin(this.__nativeRootHandle, "ShowErrorMessage");
        }
        return ok;
    }

    public void ClearCredentials()
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.Reset();
        }
        PasswordBox_ClearPassword(this.PassBox);
        UIElement_Focus(this.UserBox);
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
