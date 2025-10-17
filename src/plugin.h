// Copyright (c) 2017-2024 Manuel Schneider

#pragma once
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <albert/extensionplugin.h>
#include <albert/globalqueryhandler.h>
#include <albert/notification.h>
namespace albert { class StandardItem; }

class Plugin : public albert::ExtensionPlugin,
               public albert::GlobalQueryHandler
{
    ALBERT_PLUGIN

public:

    Plugin();
    ~Plugin();

    QWidget *buildConfigWidget() override;

    QString synopsis(const QString &) const override;
    void setTrigger(const QString&) override;
    QString defaultTrigger() const override;
    std::vector<albert::RankItem> rankItems(albert::QueryContext &) override;
    std::vector<std::shared_ptr<albert::Item>> handleEmptyQuery() override;

    uint defaultDuration() const;
    void setDefaultDuration(uint minutes);

private:
    QString makeActionName(uint minutes) const;
    std::shared_ptr<albert::StandardItem> makeDefaultItem();
    void start(uint minutes);
    void stop();
    bool isActive() const;

    QProcess process;
    QTimer timer;
    albert::Notification notification;
    QStringList commandline;
    QString trigger;
    uint default_duration_;

    struct {
        QString caffeine;
        QString sleep_inhibition;
        QString activate_sleep_inhibition;
        QString activate_sleep_inhibition_for_n_minutes;
        QString deactivate_sleep_inhibition;
    } const strings;

};
