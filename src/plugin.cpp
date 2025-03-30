// Copyright (c) 2017-2024 Manuel Schneider

#include "plugin.h"
#include "ui_configwidget.h"
#include <QStandardPaths>
#include <albert/logging.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/standarditem.h>
ALBERT_LOGGING_CATEGORY("caffeine")
using namespace albert;
using namespace std;

const QStringList Plugin::icon_urls = {"gen:?text=☕️"};

static QString durationString(uint min)
{
    const auto &[h, m] = div(min, 60);
    QStringList parts;
    if (h > 0) parts.append(Plugin::tr("%n hour(s)",   "accusative case", h));
    if (m > 0) parts.append(Plugin::tr("%n minute(s)", "accusative case", m));
    return parts.join(Plugin::tr(" and "));
}

static uint parseDurationString(const QString &s)
{
    static QRegularExpression re_nat(R"(^(?:(\d+)h\ *)?(?:(\d+)m)?$)");
    static QRegularExpression re_dig(R"(^(?|(\d+):(\d*)|()(\d+))$)");

    uint minutes = 0;
    if (auto m = re_nat.match(s); m.hasMatch() && m.capturedLength())  // required because all optional matches empty string
    {
        if (m.capturedLength(1)) minutes += m.captured(1).toInt() * 60;  // hours
        if (m.capturedLength(2)) minutes += m.captured(2).toInt();       // minutes
    }
    else if (m = re_dig.match(s); m.hasMatch())
    {
        // hasCaptured is 6.3
        if (m.capturedLength(1)) minutes += m.captured(1).toInt() * 60;  // hours
        if (m.capturedLength(2)) minutes += m.captured(2).toInt();       // minutes
    }
    return minutes;
}

Plugin::Plugin():
    strings({
        .caffeine=tr("Caffeine"),
        .sleep_inhibition=tr("Sleep inhibition"),
        .activate_sleep_inhibition=tr("Activate sleep inhibition"),
        .activate_sleep_inhibition_for_n_minutes=tr("Activate sleep inhibition for %1"),
        .deactivate_sleep_inhibition=tr("Deactivate sleep inhibition")
    })
{
#if defined(Q_OS_MAC)
    process.setProgram("caffeinate");
    process.setArguments({"-d", "-i"});
#elif defined(Q_OS_UNIX)
    process.setProgram("systemd-inhibit");
    process.setArguments({"--what=idle:sleep",
                          QString("--who=%1").arg(QCoreApplication::applicationName()),
                          "--why=User",
                          "sleep",
                          "infinity"});
#else
    throw runtime_error("Unsupported OS");
#endif

    if (auto e = QStandardPaths::findExecutable(process.program()); e.isEmpty())
        throw runtime_error(process.program().toStdString() + " not found");

    restore_default_timeout(settings());

    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, this, [this]{ stop(); });
    QObject::connect(&notification, &Notification::activated, this, [this]{ stop(); });

    notification.setTitle(name());
}

Plugin::~Plugin()
{
    stop();
}

QWidget* Plugin::buildConfigWidget()
{
    auto *w = new QWidget;
    Ui::ConfigWidget ui;
    ui.setupUi(w);
    ALBERT_PROPERTY_CONNECT_SPINBOX(this, default_timeout, ui.spinBox_minutes)
    return w;
}

QString Plugin::synopsis(const QString &) const { return tr("[duration]"); }

void Plugin::setTrigger(const QString &t) { trigger = t; }

QString Plugin::defaultTrigger() const { return tr("si ", "abbr of name()"); }

void Plugin::start(uint minutes)
{
    stop();

    process.start();
    if (!process.waitForStarted(1000) || process.state() != QProcess::Running)
        WARN << "Sleep inhibition failed" << process.errorString();
    else
    {
        INFO << "Sleep inhibition activated";

        notification.setText(tr("Sleep inhibition activated.") + "\n"
                             + tr("Click to deactivate."));
        notification.dismiss();
        notification.send();

        if (minutes > 0)
            timer.start(minutes * 60 * 1000);
    }
}

void Plugin::stop()
{
    if (isActive())
    {
        INFO << "Sleep inhibition deactivated";

        notification.setText(tr("Sleep inhibition deactivated."));
        notification.dismiss();
        notification.send();

        process.kill();
        process.waitForFinished();
        timer.stop();
    }
}

bool Plugin::isActive() const { return process.state() == QProcess::Running; }

QString Plugin::makeActionName(uint minutes) const
{
    if (minutes)
        return strings.activate_sleep_inhibition_for_n_minutes.arg(durationString(minutes));
    else
        return strings.activate_sleep_inhibition;
}

shared_ptr<Item> Plugin::makeTriggerItem(const QString action_name, function<void()> action)
{
    return StandardItem::make(id(), name(), action_name, icon_urls,
                              {{ id(), action_name, action }});
}

shared_ptr<Item> Plugin::makeGlobalItem(const QString action_name, function<void()> action)
{
    return StandardItem::make(id(), name(), action_name, name(), icon_urls,
                              {{ id(), action_name, action }});
}

void Plugin::handleTriggerQuery(Query &query)
{
    if (auto s = query.string().trimmed(); s.isEmpty())

        if (isActive())
            query.add(makeTriggerItem(strings.deactivate_sleep_inhibition,
                                      [this]{ stop(); }));
        else
            query.add(makeTriggerItem(makeActionName(default_timeout_),
                                      [this]{ start(default_timeout_); }));

    else if (auto minutes = parseDurationString(s); minutes)

        query.add(makeTriggerItem(makeActionName(minutes),
                                  [this, minutes]{ start(minutes); }));
}

vector<RankItem> Plugin::handleGlobalQuery(const Query &query)
{
    vector<RankItem> r;
    if (auto m = Matcher(query).match(strings.caffeine, strings.sleep_inhibition); m)
    {
        if (isActive())
            r.emplace_back(makeTriggerItem(strings.deactivate_sleep_inhibition,
                                           [this]{ stop(); }),
                           m);
        else
            r.emplace_back(makeTriggerItem(makeActionName(default_timeout_),
                                           [this]{ start(default_timeout_); }),
                           m);
    }
    return r;
}

vector<shared_ptr<Item>> Plugin::handleEmptyQuery()
{
    vector<shared_ptr<Item>> r;
    if (isActive())
        r.emplace_back(makeTriggerItem(strings.deactivate_sleep_inhibition, [this]{ stop(); }));
    return r;
}
