#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_ChineseCAD.h"
#include "clipper2/clipper.core.h"

class ChineseCAD : public QMainWindow
{
    Q_OBJECT

public:
    ChineseCAD(QWidget *parent = nullptr);
    ~ChineseCAD();

private:
    Ui::ChineseCADClass ui;
};

