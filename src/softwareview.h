// SPDX-License-Identifier: MIT
#pragma once

#include <QWidget>

class TerminalSession;

// CPU fallback renderer: draws the session with QPainter, in device pixels.
class SoftwareView : public QWidget
{
    Q_OBJECT
public:
    explicit SoftwareView(TerminalSession *session, QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    TerminalSession *m_session;
};
