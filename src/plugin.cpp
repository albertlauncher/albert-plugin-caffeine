// Copyright (c) 2017-2025 Manuel Schneider

#include "plugin.h"
#include "ui_configwidget.h"
#include <QSettings>
#include <QStandardPaths>
#include <albert/icon.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/standarditem.h>
#include <albert/widgetsutil.h>
ALBERT_LOGGING_CATEGORY("caffeine")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

namespace {
static unique_ptr<Icon> makeIcon() { return Icon::grapheme(u"☕️"_s); }
const auto ck_default_duration = "default_duration";
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

    default_duration_ = settings()->value(ck_default_duration, 60u).value<uint>();

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

    bindWidget(ui.spinBox_defaultDuration, this, &Plugin::defaultDuration, &Plugin::setDefaultDuration);

    return w;
}

QString Plugin::synopsis(const QString &) const { return tr("[duration]"); }

void Plugin::onTriggerChanged(const QString &t) { trigger = t; }

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
                                  makeIcon,
                                  {{id(), strings.deactivate_sleep_inhibition,
                                    [this]{ stop(); }}},
                                  trigger
                                  );
    else
        return StandardItem::make(id(),
                                  name(),
                                  makeActionName(default_duration_),
                                  makeIcon,
                                  {{id(), strings.activate_sleep_inhibition,
                                    [this]{ start(default_duration_); } }},
                                  trigger
                                  );
}

vector<RankItem> Plugin::rankItems(QueryContext ctx)
{
    vector<RankItem> r;
    if (ctx.trigger().isEmpty())
    {
        if (auto m = Matcher(ctx).match(strings.caffeine, strings.sleep_inhibition); m)
            r.emplace_back(makeDefaultItem(), m);
    }

    else if (auto s = ctx.query().trimmed(); s.isEmpty())
    {
        auto item = makeDefaultItem();
        item->setInputActionText(u""_s);  // remove input action text
        r.emplace_back(::move(item), .0);
    }

    else if (auto minutes = parseDurationString(s); minutes)
    {
        r.emplace_back(StandardItem::make(id(),
                                          name(),
                                          makeActionName(minutes),
                                          makeIcon,
                                          {{id(), strings.activate_sleep_inhibition,
                                            [this, minutes] { start(minutes); }}},
                                          u""_s  // no completion
                                          )
                       , .0);
    }
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

uint Plugin::defaultDuration() const { return default_duration_; }

void Plugin::setDefaultDuration(uint v)
{
    if (default_duration_ != v)
    {
        default_duration_ = v;
        settings()->setValue(ck_default_duration, v);
    }
}
