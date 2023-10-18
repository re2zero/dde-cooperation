﻿#include "startwidget.h"
#include "../type_defines.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QDebug>
#include <QToolButton>
#include <QStackedWidget>
#include <QCheckBox>
#include <QTextBrowser>

#pragma execution_character_set("utf-8")

StartWidget::StartWidget(QWidget *parent)
    : QFrame(parent)
{
    initUI();
}

StartWidget::~StartWidget()
{
}

void StartWidget::initUI()
{
    setStyleSheet("background-color: white; border-radius: 10px;");

    QVBoxLayout *mainLayout = new QVBoxLayout();
    setLayout(mainLayout);
    mainLayout->setSpacing(0);

    QLabel *iconLabel = new QLabel(this);
    iconLabel->setPixmap(QIcon(":/icon/picture-home.png").pixmap(200, 160));
    iconLabel->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);

    QLabel *textLabel1 = new QLabel("UOS迁移工具", this);
    QFont font;
    font.setPointSize(16);
    font.setWeight(QFont::DemiBold);
    textLabel1->setFont(font);
    textLabel1->setAlignment(Qt::AlignCenter);

    QLabel *textLabel2 = new QLabel("UOS迁移工具,一键将您的文件，个人数据和应用数据迁移到\nUOS，助您无缝更换系统。", this);
    textLabel2->setAlignment(Qt::AlignTop | Qt::AlignCenter);
    font.setPointSize(10);
    font.setWeight(QFont::Thin);
    textLabel2->setFont(font);

    QLabel *label = new QLabel(this);
    font.setPointSize(9);
    label->setFont(font);
    label->setText("我已阅读并同意<a href=\"https://\" style=\"text-decoration:none;\">《UOS迁移工具》</a>");
    connect(label, &QLabel::linkActivated, this, &StartWidget::nextPage);

    checkBox = new QCheckBox(this);
    QHBoxLayout *checkLayout = new QHBoxLayout();

    checkLayout->addStretch();
    checkLayout->addWidget(checkBox);
    checkLayout->addWidget(label);
    checkLayout->addSpacing(240);
    checkLayout->setMargin(10);
    checkLayout->setAlignment(Qt::AlignBottom);

    nextButton = new QToolButton(this);
    nextButton->setText("下一步");
    nextButton->setFixedSize(250, 36);
    nextButton->setStyleSheet("background-color: lightgray;");
    nextButton->setEnabled(false);
    connect(nextButton, &QToolButton::clicked, this, &StartWidget::nextPage);
    connect(checkBox, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::Checked)
            nextButton->setEnabled(true);
        else
            nextButton->setEnabled(false);
    });

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(nextButton, Qt::AlignCenter);

    mainLayout->addSpacing(50);
    mainLayout->addWidget(iconLabel);
    mainLayout->addWidget(textLabel1);
    mainLayout->addWidget(textLabel2);
    mainLayout->addSpacing(60);
    mainLayout->addLayout(checkLayout);
    mainLayout->addLayout(buttonLayout);
}

void StartWidget::nextPage()
{
    QStackedWidget *stackedWidget = qobject_cast<QStackedWidget *>(this->parent());
    if (stackedWidget) {
        int page = checkBox->checkState() == Qt::Checked ? PageName::choosewidget : PageName::licensewidget;
        stackedWidget->setCurrentIndex(stackedWidget->currentIndex() + page);
    } else {
        qWarning() << "Jump to next page failed, qobject_cast<QStackedWidget *>(this->parent()) = nullptr";
    }
}

void StartWidget::themeChanged(int theme)
{
    //light
    if (theme == 1) {
        setStyleSheet("background-color: white; border-radius: 10px;");
        nextButton->setStyleSheet("background-color: lightgray;");
    } else {
        //dark
        nextButton->setStyleSheet("background-color: rgba(0, 0, 0, 0.08);");
        setStyleSheet("background-color: rgb(37, 37, 37); border-radius: 10px;");
    }
}
