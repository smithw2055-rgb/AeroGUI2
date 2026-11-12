////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __TICTACTOE_VIEWMODEL_H__
#define __TICTACTOE_VIEWMODEL_H__


#include <NsCore/Noesis.h>
#include <NsCore/ReflectionDeclare.h>
#include <NsCore/ReflectionDeclareEnum.h>
#include <NsApp/DelegateCommand.h>
#include <NsApp/NotifyPropertyChangedBase.h>


namespace TicTacToe
{

////////////////////////////////////////////////////////////////////////////////////////////////////
enum class State
{
    Player1 = 1,
    Player2 = 2,
    Tie,
    WinRow0,
    WinRow1,
    WinRow2,
    WinCol0,
    WinCol1,
    WinCol2,
    WinDiag1,
    WinDiag2
};

////////////////////////////////////////////////////////////////////////////////////////////////////
class ViewModel final: public NoesisApp::NotifyPropertyChangedBase
{
public:
    ViewModel();

    const NoesisApp::DelegateCommand* GetStartCommand() const;
    const NoesisApp::DelegateCommand* GetCheckCommand() const;

    State GetState() const;

    int GetScorePlayer1() const;
    int GetScorePlayer2() const;
    int GetScoreTies() const;

    int GetCell0() const;
    int GetCell1() const;
    int GetCell2() const;
    int GetCell3() const;
    int GetCell4() const;
    int GetCell5() const;
    int GetCell6() const;
    int GetCell7() const;
    int GetCell8() const;

    NS_IMPLEMENT_INTERFACE_FIXUP

private:
    void OnStart(BaseComponent* param);
    void OnCheck(BaseComponent* param);

    State CheckWin();
    bool CheckRow(int row);
    bool CheckCol(int col);
    bool CheckDiag(int start, int end);
    bool CheckCell(int row, int col);
    bool CheckTie();

    State Win(State state);

private:
    NoesisApp::DelegateCommand _startCommand;
    NoesisApp::DelegateCommand _checkCommand;
    State _state;
    int _scorePlayer1;
    int _scorePlayer2;
    int _scoreTies;

    int _board[9];

    Noesis::PropertyChangedEventHandler _changed;

    NS_DECLARE_REFLECTION(ViewModel, NotifyPropertyChangedBase)
};

}

NS_DECLARE_REFLECTION_ENUM(TicTacToe::State)


#endif
