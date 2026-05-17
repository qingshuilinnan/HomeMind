#include "loginwindow.h"
#include "ui_loginwindow.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QCheckBox>
#include <QAction>

// 初始化静态成员变量
QString LoginWindow::currentUsername = "";

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWindow)
    , db(new Database())
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    db->open();

    // 连接显示密码按钮
    connect(ui->togglePasswordButton, &QPushButton::clicked, this, &LoginWindow::togglePasswordVisibility);

    // 密码框聚焦时联动按钮边框变色
    connect(ui->passwordLineEdit, &QLineEdit::selectionChanged, this, [this]() {
        ui->togglePasswordButton->setStyleSheet(
            "QPushButton { background-color: white; border: 1.5px solid #409eff; border-left: 1px solid #e4e7ed; "
            "border-top-left-radius: 0px; border-bottom-left-radius: 0px; "
            "border-top-right-radius: 10px; border-bottom-right-radius: 10px; "
            "color: #409eff; font-size: 16px; padding: 0px; }"
        );
    });
    connect(ui->passwordLineEdit, &QLineEdit::editingFinished, this, [this]() {
        ui->togglePasswordButton->setStyleSheet(
            "QPushButton { background-color: white; border: 1.5px solid #dcdfe6; border-left: 1px solid #e4e7ed; "
            "border-top-left-radius: 0px; border-bottom-left-radius: 0px; "
            "border-top-right-radius: 10px; border-bottom-right-radius: 10px; "
            "color: #909399; font-size: 16px; padding: 0px; }"
            "QPushButton:hover { color: #409eff; border-color: #c0c4cc; }"
        );
    });
}

void LoginWindow::togglePasswordVisibility()
{
    if (ui->passwordLineEdit->echoMode() == QLineEdit::Password) {
        ui->passwordLineEdit->setEchoMode(QLineEdit::Normal);
        ui->togglePasswordButton->setText("👁️‍🗨️"); // 切换图标表示正在显示
    } else {
        ui->passwordLineEdit->setEchoMode(QLineEdit::Password);
        ui->togglePasswordButton->setText("👁️");
    }
}

LoginWindow::~LoginWindow()
{
    delete ui;
    delete db;
}

void LoginWindow::on_loginButton_clicked()
{
    QString username = ui->usernameLineEdit->text();
    QString password = ui->passwordLineEdit->text();
    
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入用户名和密码");
        return;
    }
    
    if (db->loginUser(username, password)) {
        currentUsername = username; // 保存当前登录的用户名
        
        // 处理“保持登录”逻辑
        if (ui->keepLoginCheckBox->isChecked()) {
            db->setAutoLogin(username, true);
        } else {
            db->setAutoLogin(username, false);
        }
        
        emit loginSuccess();
        close();
    } else {
        QMessageBox::warning(this, "错误", "用户名或密码错误");
    }
}

void LoginWindow::on_registerButton_clicked()
{
    QString username = ui->usernameLineEdit->text();
    QString password = ui->passwordLineEdit->text();
    
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入用户名和密码");
        return;
    }
    
    if (db->registerUser(username, password)) {
        QMessageBox::information(this, "成功", "注册成功，请登录");
    } else {
        QMessageBox::warning(this, "错误", "用户名已存在");
    }
}

void LoginWindow::on_resetPasswordButton_clicked()
{
    QString username = ui->usernameLineEdit->text();
    
    if (username.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入用户名");
        return;
    }
    
    bool ok;
    QString newPassword = QInputDialog::getText(this, "重置密码", "请输入新密码:", QLineEdit::Password, "", &ok);
    if (ok && !newPassword.isEmpty()) {
        if (db->resetPassword(username, newPassword)) {
            QMessageBox::information(this, "成功", "密码重置成功");
        } else {
            QMessageBox::warning(this, "错误", "用户名不存在");
        }
    }
}