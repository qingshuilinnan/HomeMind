#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
#include "database.h"

namespace Ui {
class LoginWindow;
}

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow();
    
    static QString currentUsername; // 存储当前登录的用户名

private slots:
    void on_loginButton_clicked();
    void on_registerButton_clicked();
    void on_resetPasswordButton_clicked();
    void togglePasswordVisibility();

private:signals:
    void loginSuccess();

private:
    Ui::LoginWindow *ui;
    Database *db;
};

#endif // LOGINWINDOW_H