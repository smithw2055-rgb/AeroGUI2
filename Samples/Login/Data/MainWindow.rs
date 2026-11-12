module Login;

import Aero.UI;

[Observable]
public class ViewModel
{
    private DelegateCommand _loginCommand;

    public DelegateCommand LoginCommand { get { return this._loginCommand; } }
    public string AccountName { get; set; } = "";
    public string Message { get; set; } = "";
    public bool LoginSuccessful { get; set; } = false;
    public int LoginAttempts { get; set; } = 0;

    public ViewModel()
    {
        this._loginCommand = new DelegateCommand();
        this._loginCommand.SetExecuteFunc(this.Login);

        this.AccountName = "";
        this.Message = "";
        this.LoginSuccessful = false;
        this.LoginAttempts = 0;
    }

    public void Login(handle passBox)
    {
        string password = PasswordBox_GetPassword(passBox);
        this.CheckCredentials(password);
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
    public ViewModel DataContext { get; set; } = null;
    public ViewModel ViewModel { get { return this.DataContext; } }

    public MainWindow()
    {
        Application_SetResources("Resources.xaml");
        this.InitializeComponent();

        this.DataContext = new ViewModel();
        this.SetDataContext<ViewModel>(this.DataContext);
    }

    public void OnInitialized()
    {
    }

    public void SubmitLogin()
    {
        this.DataContext.Login(this.PassBox);
    }

    public bool LoginWithPassword(string password)
    {
        return this.DataContext.CheckCredentials(password);
    }

    public void ClearCredentials()
    {
        if (this.DataContext != null)
        {
            this.DataContext.Reset();
        }
        PasswordBox_ClearPassword(this.PassBox);
        UIElement_Focus(this.UserBox);
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
