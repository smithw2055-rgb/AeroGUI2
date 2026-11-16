module Login;

import Aero.UI;

[Observable]
public class ViewModel : ViewModelBase
{
    private DelegateCommand _loginCommand;
    private string _accountName = "";
    private string _message = "";
    private bool _loginSuccessful = false;
    private int _loginAttempts = 0;

    public DelegateCommand LoginCommand { get { return this._loginCommand; } }

    public string AccountName
    {
        get { return this._accountName; }
        set { this.SetProperty(ref this._accountName, value, "AccountName"); }
    }

    public string Message
    {
        get { return this._message; }
        set { this.SetProperty(ref this._message, value, "Message"); }
    }

    public bool LoginSuccessful
    {
        get { return this._loginSuccessful; }
        set { this.SetProperty(ref this._loginSuccessful, value, "LoginSuccessful"); }
    }

    public int LoginAttempts
    {
        get { return this._loginAttempts; }
        set { this.SetProperty(ref this._loginAttempts, value, "LoginAttempts"); }
    }

    public ViewModel()
    {
        this._loginCommand = new DelegateCommand((object param) => { this.Login(param); });
        this._accountName = "";
        this._message = "";
        this._loginSuccessful = false;
        this._loginAttempts = 0;
    }

    public void Login(object passBox)
    {
        string password = "";
        if (passBox is PasswordBox)
        {
            var box = (PasswordBox)passBox;
            password = box.Password;
        }
        else if (passBox is BoxedString)
        {
            var boxed = (BoxedString)passBox;
            password = boxed.Value;
        }
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

public partial class MainWindow : Page
{
    private ViewModel _vm;

    public ViewModel VM { get { return this._vm; } }
    public ViewModel ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new ViewModel();
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void OnInitialized()
    {
    }

    public void SubmitLogin()
    {
        if (this.VM != null)
        {
            this.VM.Login(this.PassBox);
        }
    }

    public bool LoginWithPassword(string password)
    {
        return this.VM != null ? this.VM.CheckCredentials(password) : false;
    }

    public void ClearCredentials()
    {
        if (this.VM != null)
        {
            this.VM.Reset();
        }
        if (this.PassBox != null)
        {
            this.PassBox.Password = "";
        }
        if (this.UserBox != null)
        {
            this.UserBox.Focus();
        }
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
