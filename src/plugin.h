// Copyright (c) 2017-2024 Manuel Schneider

#pragma once
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <albert/extensionplugin.h>
#include <albert/globalqueryhandler.h>
#include <albert/notification.h>
#include <albert/property.h>
namespace albert::util { class StandardItem; }

class Plugin : public albert::util::ExtensionPlugin,
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
    void handleTriggerQuery(albert::Query &) override;
    std::vector<albert::RankItem> handleGlobalQuery(const albert::Query &) override;
    std::vector<std::shared_ptr<albert::Item>> handleEmptyQuery() override;

private:
    QString makeActionName(uint minutes) const;
    std::shared_ptr<albert::util::StandardItem> makeDefaultItem();
    void start(uint minutes);
    void stop();
    bool isActive() const;

    QProcess process;
    QTimer timer;
    albert::util::Notification notification;
    QStringList commandline;
    QString trigger;

    ALBERT_PLUGIN_PROPERTY(uint, default_timeout, 60)

    struct{
        QString caffeine;
        QString sleep_inhibition;
        QString activate_sleep_inhibition;
        QString activate_sleep_inhibition_for_n_minutes;
        QString deactivate_sleep_inhibition;
    } const strings;

};
