// Copyright (c) 2017-2025 Manuel Schneider

#include "plugin.h"
#include "ui_configwidget.h"
#include <QStandardPaths>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/standarditem.h>
#include <albert/widgetsutil.h>
ALBERT_LOGGING_CATEGORY("caffeine")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;
using namespace util;

namespace {
static const QStringList icon_urls = {u"gen:?text=☕️"_s};
}

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
    static QRegularExpression re_nat(uR"(^(?:(\d+)h\ *)?(?:(\d+)m)?$)"_s);
    static QRegularExpression re_dig(uR"(^(?|(\d+):(\d*)|()(\d+))$)"_s);

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
    process.setProgram(u"caffeinate"_s);
    process.setArguments({u"-d"_s, u"-i"_s});
#elif defined(Q_OS_UNIX)
    process.setProgram(u"systemd-inhibit"_s);
    process.setArguments({u"--what=idle:sleep"_s,
                          QString(u"--who=%1"_s).arg(QCoreApplication::applicationName()),
                          u"--why=User"_s,
                          u"sleep"_s,
                          u"infinity"_s});
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

    bind(ui.spinBox_minutes,
         this,
         &Plugin::default_timeout,
         &Plugin::set_default_timeout,
         &Plugin::default_timeout_changed);

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

        notification.setText(tr("Sleep inhibition activated.") + QChar::LineFeed
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

shared_ptr<StandardItem> Plugin::makeDefaultItem()
{
    if (isActive())
        return StandardItem::make(id(),
                                  name(),
                                  strings.deactivate_sleep_inhibition,
                                  icon_urls,
                                  {{id(), strings.deactivate_sleep_inhibition,
                                    [this]{ stop(); }}},
                                  trigger
                                  );
    else
        return StandardItem::make(id(),
                                  name(),
                                  makeActionName(default_timeout_),
                                  icon_urls,
                                  {{id(), strings.activate_sleep_inhibition,
                                    [this]{ start(default_timeout_); } }},
                                  trigger
                                  );
}

void Plugin::handleTriggerQuery(Query &query)
{
    if (auto s = query.string().trimmed(); s.isEmpty())
    {
        auto item = makeDefaultItem();
        item->setInputActionText(u""_s);  // remove input action text
        query.add(::move(item));
    }

    else if (auto minutes = parseDurationString(s); minutes)
    {
        query.add(StandardItem::make(id(),
                                     name(),
                                     makeActionName(minutes),
                                     icon_urls,
                                     {{id(), strings.activate_sleep_inhibition,
                                       [this, minutes] { start(minutes); }}},
                                     u""_s  // no completion
                                     ));
    }
}

vector<RankItem> Plugin::handleGlobalQuery(const Query &query)
{
    vector<RankItem> r;
    if (auto m = Matcher(query).match(strings.caffeine, strings.sleep_inhibition); m)
        r.emplace_back(makeDefaultItem(), m);
    return r;
}

vector<shared_ptr<Item>> Plugin::handleEmptyQuery()
{
    vector<shared_ptr<Item>> r;
    if (isActive()){
        auto item = makeDefaultItem();
        item->setInputActionText(u""_s);  // remove completion
        r.emplace_back(::move(item));
    }
    return r;
}
