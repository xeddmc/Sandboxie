#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_CompressDialog.h"
#include "SbiePlusAPI.h"

class CCompressDialog : public QDialog
{
	Q_OBJECT

public:
	CCompressDialog(QWidget *parent = Q_NULLPTR);
	~CCompressDialog();

	QString GetFormat();
	int GetLevel();
	bool MakeSolid();

	void SetMustEncrypt();
	bool UseEncryption();

private slots:
	void OnFormatChanged(int index);

private:
	Ui::CompressDialog ui;
};
