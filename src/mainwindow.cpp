/*******************************************************************************
 * This file is part of Re_Sync Next.
 *
 * Copyright (C) 2011-2013  Andrey Efremov <duxus@yandex.ru>
 *
 * Re_Sync is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Re_Sync is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Re_Sync.  If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "script.h"
#include <QDesktopWidget>
#include <QGraphicsSceneMouseEvent>
#include <QDragEnterEvent>
#include <QUrl>
#include <QSettings>
#include <QFileDialog>
#include <QTextStream>
#include <QTextCodec>
#include <QtGui>
#include <QSvgGenerator>
#include <QMessageBox>

QString UrlToPath(const QUrl &url);

const qreal HEIGHT = 50.0, SCALE = 0.002;
const QRectF DEFAULT_RECT(0.0, 0.0, 1.0, HEIGHT * 3.0 + 20.0);
const QStringList FILETYPES = QStringList() << "ass" << "ssa" << "srt";
const QString FILETYPES_FILTER  = "Субтитры (*." + FILETYPES.join(" *.") + ")";
const QString DEFAULT_DIR_KEY   = "DefaultDir",
              MIN_DURATION_KEY  = "MinDuration",
              MAX_OFFSET_KEY    = "MaxOffset",
              MAX_DESYNC_KEY    = "MaxDesync",
              MAX_SHIFT_KEY     = "MaxShift",
              SKIP_COMMENTS_KEY = "SkipComments",
              SKIP_LYRICS_KEY   = "SkipLyrics";


class GraphicsEventGroupItem : public QGraphicsItemGroup
{
public:
    GraphicsEventGroupItem() :
        QGraphicsItemGroup(),
        _prev(nullptr),
        _next(nullptr),
        _eventGroup(nullptr),
        _rect(this),
        _text(this)
    {
        this->init();
    }

    ~GraphicsEventGroupItem();

    void setPrev(GraphicsEventGroupItem * const prev)
    {
        _prev = prev;
    }
    void setNext(GraphicsEventGroupItem * const next)
    {
        _next = next;
    }
    void setEventGroup(EventGroup * const eventGroup)
    {
        _eventGroup = eventGroup;
    }

    QGraphicsRectItem *rect()
    {
        return &_rect;
    }
    QGraphicsTextItem *text()
    {
        return &_text;
    }
    EventGroup *eventGroup()
    {
        return _eventGroup;
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event)
    {
        _corX = QGraphicsItem::mapToItem(this, event->pos()).x();

        if (nullptr != _prev) _minX = _prev->x() + _prev->rect()->rect().width();
        else _minX = 0.0;

        if (nullptr != _next) _maxX = _next->x() - this->rect()->rect().width();
        else _maxX = this->scene()->width() - this->rect()->rect().width();

        QGraphicsItemGroup::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event)
    {
        static qreal dx;
        dx = qMax(_minX - this->x(), QGraphicsItem::mapToItem(this, event->pos()).x() - _corX);
        if (event->buttons() & Qt::LeftButton)
        {
            this->moveByX(dx, true);
        }
        else
        {
            dx = qMin(_maxX - this->x(), dx);
            this->moveByX(dx, false);
        }
    }

    void setToolTip(const QString &toolTip)
    {
        _rect.setToolTip(toolTip);
        _text.setToolTip(toolTip);
    }
    void setCursor(const QCursor &cursor)
    {
        _rect.setCursor(cursor);
        _text.setCursor(cursor);
    }

    void setMovable(const bool enabled)
    {
        //QGraphicsItem::ItemIsSelectable
        if (enabled) this->setFlags(this->flags() | QGraphicsItem::ItemIsMovable);
        else this->setFlags(this->flags() & ~QGraphicsItem::ItemIsMovable);
    }

    void moveByX(const qreal dx, const bool moveNext = false)
    {
        this->moveBy(dx, 0.0);
        if (moveNext && nullptr != _next) _next->moveByX(dx, moveNext);
    }

private:
    GraphicsEventGroupItem *_prev, *_next;
    EventGroup *_eventGroup;
    QGraphicsRectItem _rect;
    QGraphicsTextItem _text;
    qreal _corX, _minX, _maxX;

    void init()
    {
        this->addToGroup(&_rect);
        this->addToGroup(&_text);
    }
};

//! @bug: https://bugreports.qt.io/browse/QTCREATORBUG-19741
GraphicsEventGroupItem::~GraphicsEventGroupItem() {}


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    _openState(0u)
{
    ui->setupUi(this);

    ui->spinMinDuration->setValue(_settings.value(MIN_DURATION_KEY, ui->spinMinDuration->value()).toInt());
    ui->spinMaxOffset  ->setValue(_settings.value(MAX_OFFSET_KEY,   ui->spinMaxOffset->value()).toInt());
    ui->spinMaxDesync  ->setValue(_settings.value(MAX_DESYNC_KEY,   ui->spinMaxDesync->value()).toInt());
    ui->spinMaxShift   ->setValue(_settings.value(MAX_SHIFT_KEY,    ui->spinMaxShift->value()).toInt());
    ui->chbSkipComments->setChecked(_settings.value(SKIP_COMMENTS_KEY, ui->chbSkipComments->isChecked()).toBool());
    ui->chbSkipLyrics  ->setChecked(_settings.value(SKIP_LYRICS_KEY,   ui->chbSkipLyrics->isChecked()).toBool());

    _sync.position      = 0;
    _sync.title         = "Синхронные";
    _sync.borderColor   = "#197F3B";
    _sync.bgColor       = "#B2FFCC";
    _sync.hlColor       = "#60F291";

    _desync.position    = 1;
    _desync.title       = "Синхронизируемые";
    _desync.borderColor = "#992D2D";
    _desync.bgColor     = "#FFCCCC";
    _desync.hlColor     = "#FF8C8C";
    _desync.connected   = true;

    _result.position    = 2;
    _result.title       = "Результат";
    _result.borderColor = "#7F6E19";
    _result.bgColor     = "#FFF2B2";
    _result.hlColor     = "#F2DA60";

    this->move(QApplication::desktop()->screenGeometry().center() - this->rect().center());

    ui->graphicsView->scale(SCALE, 1.0);
    ui->graphicsView->setScene(new QGraphicsScene());
    ui->graphicsView->setSceneRect(DEFAULT_RECT);
}

MainWindow::~MainWindow()
{
    _settings.setValue(MIN_DURATION_KEY,  ui->spinMinDuration->value());
    _settings.setValue(MAX_OFFSET_KEY,    ui->spinMaxOffset->value());
    _settings.setValue(MAX_DESYNC_KEY,    ui->spinMaxDesync->value());
    _settings.setValue(MAX_SHIFT_KEY,     ui->spinMaxShift->value());
    _settings.setValue(SKIP_COMMENTS_KEY, ui->chbSkipComments->isChecked());
    _settings.setValue(SKIP_LYRICS_KEY,   ui->chbSkipLyrics->isChecked());

    delete ui;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls)
        {
            if (!UrlToPath(url).isEmpty())
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        for (int i = 0; i < event->mimeData()->urls().length() && i < 2; ++i)
        {
            const QString path = UrlToPath(event->mimeData()->urls().at(i));
            if (!path.isEmpty())
            {
                if (i) this->openFile(path, _desync);
                else   this->openFile(path, _sync);
            }
        }
        event->acceptProposedAction();
    }
}

void MainWindow::on_btOpenSynced_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this,
                                                          "Выберите файл",
                                                          _settings.value(DEFAULT_DIR_KEY).toString(),
                                                          FILETYPES_FILTER);

    if (fileName.isEmpty()) return;
    _settings.setValue(DEFAULT_DIR_KEY, QFileInfo(fileName).absolutePath());

    this->openFile(fileName, _sync);
}

void MainWindow::on_btOpenDesynced_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this,
                                                          "Выберите файл",
                                                          _settings.value(DEFAULT_DIR_KEY).toString(),
                                                          FILETYPES_FILTER);

    if (fileName.isEmpty()) return;
    _settings.setValue(DEFAULT_DIR_KEY, QFileInfo(fileName).absolutePath());

    this->openFile(fileName, _desync);
}

void MainWindow::on_btAutosync_clicked()
{
    if (_sync.groups.isEmpty() || _desync.groups.isEmpty())
    {
        QMessageBox::warning(this, "Предупреждение", "Для синхронизации необходимо открыть оба файла");
        return;
    }

    if (nullptr != _result.graph) ui->graphicsView->scene()->removeItem(_result.graph);
    _result.clear();

    for (QGraphicsItem * const item : qAsConst(_desync.items))
    {
        GraphicsEventGroupItem * const egItem = dynamic_cast<GraphicsEventGroupItem *>(item);
        egItem->eventGroup()->shift( static_cast<int>(egItem->x()) - static_cast<int>(egItem->eventGroup()->start()) );
        //egItem->eventGroup()->applyShift(); // Портит саб, если вызывать тут
    }

    // Поиск НОП
    DesyncGroupList desyncPoints;
    LCS(_sync.groups, _desync.groups, desyncPoints, ui->spinMaxDesync->value());

    // Синхронизация
    Syncronize(_sync.groups, _desync.groups, desyncPoints, _result.groups, ui->spinMaxDesync->value(), ui->spinMaxShift->value());

    this->drawGraph(_result, &desyncPoints);

    ui->btSave->setEnabled(true);
}

void MainWindow::on_btSave_clicked()
{
    if (_result.groups.isEmpty())
    {
        QMessageBox::warning(this, "Предупреждение", "Необходимо сначала синхронизировать субтитры");
        return;
    }

    const QFileInfo fileInfo(_desync.fileName);
    const QString fileName = QFileDialog::getSaveFileName(this,
                                                          "Выберите файл",
                                                          QDir(fileInfo.path() + "/" + fileInfo.baseName() + ".sync." + fileInfo.completeSuffix()).path(),
                                                          FILETYPES_FILTER);

    if (fileName.isEmpty()) return;

    // Применение результатов
    for (EventGroup& eg : _result.groups) eg.applyShift();

    QFile fout(fileName);
    if ( !fout.open(QFile::WriteOnly | QFile::Text) )
    {
        QMessageBox::critical(this, "Ошибка", QString("Ошибка открытия файла \"%1\"").arg(fileName));
        return;
    }

    QTextStream out(&fout);
    out.setCodec( QTextCodec::codecForName("UTF-8") );
    out.setGenerateByteOrderMark(true);
    switch (_desync.scriptType)
    {
    case Script::SCR_SSA:
        if ( !Script::GenerateSSA(out, _desync.script) )
        {
            QMessageBox::critical(this, "Ошибка", QString("Ошибка вывода файла в формате SSA"));
            fout.close();
            return;
        }
        break;

    case Script::SCR_ASS:
        if ( !Script::GenerateASS(out, _desync.script) )
        {
            QMessageBox::critical(this, "Ошибка", QString("Ошибка вывода файла в формате ASS"));
            fout.close();
            return;
        }
        break;

    case Script::SCR_SRT:
        if ( !Script::GenerateSRT(out, _desync.script) )
        {
            QMessageBox::critical(this, "Ошибка", QString("Ошибка вывода файла в формате SRT"));
            fout.close();
            return;
        }
        break;

    default:
        QMessageBox::critical(this, "Ошибка", QString("Хьюстон, у нас проблема"));
        fout.close();
        return;
    }
    fout.close();

    if (nullptr != _result.graph) ui->graphicsView->scene()->removeItem(_result.graph);
    _result.clear();

    if (nullptr != _desync.graph) ui->graphicsView->scene()->removeItem(_desync.graph);
    _desync.clear();

    if (nullptr != _sync.graph) ui->graphicsView->scene()->removeItem(_sync.graph);
    _sync.clear();

    ui->graphicsView->setSceneRect(DEFAULT_RECT);

    ui->btSave->setEnabled(false);
    ui->btAutosync->setEnabled(false);
    _openState = 0u;
    ui->btOpenSynced->setText(ui->btOpenSynced->toolTip());
    ui->btOpenDesynced->setText(ui->btOpenDesynced->toolTip());
}

void MainWindow::on_btSvg_clicked()
{
    const QFileInfo fileInfo(_desync.fileName);
    const QString fileName = QFileDialog::getSaveFileName(this,
                                                          "Выберите файл",
                                                          QDir(fileInfo.path() + "/" + fileInfo.baseName() + ".svg").path(),
                                                          "Scalable Vector Graphics (*.svg)");

    if (fileName.isEmpty()) return;

    const QRectF targetRect(0.0, 0.0, ui->graphicsView->scene()->width() * SCALE, ui->graphicsView->scene()->height());
    QSvgGenerator generator;
    generator.setFileName(fileName);
    generator.setSize(targetRect.size().toSize());
    generator.setViewBox(targetRect);
    generator.setTitle(fileInfo.fileName());
    generator.setDescription("Generated by Re_Sync");

    QPainter painter(&generator);
    ui->graphicsView->scene()->render(&painter, targetRect, QRectF(), Qt::IgnoreAspectRatio);
}


void MainWindow::openFile(const QString &fileName, FileStruct &obj)
{
    if (nullptr != obj.graph) ui->graphicsView->scene()->removeItem(obj.graph);
    obj.clear();

    QFile fin(fileName);
    if ( !fin.open(QFile::ReadOnly | QFile::Text) )
    {
        QMessageBox::critical(this, "Ошибка", QString("Ошибка открытия файла \"%1\"").arg(fileName));
        return;
    }

    QTextStream in(&fin);
    obj.scriptType = Script::DetectFormat(in);

    switch (obj.scriptType)
    {
    case Script::SCR_SSA:
    case Script::SCR_ASS:
        if ( !Script::ParseSSA(in, obj.script) )
        {
            QMessageBox::critical(this, "Ошибка", QString("Файл \"%1\" не соответствует формату SSA/ASS").arg(fileName));
            fin.close();
            return;
        }
        break;

    case Script::SCR_SRT:
        if ( !Script::ParseSRT(in, obj.script) )
        {
            QMessageBox::critical(this, "Ошибка", QString("Файл \"%1\" не соответствует формату SRT").arg(fileName));
            fin.close();
            return;
        }
        break;

    default:
        QMessageBox::critical(this, "Ошибка", QString("Файл \"%1\" имеет неизвестный формат").arg(fileName));
        fin.close();
        return;
    }
    fin.close();

    obj.fileName = fileName;
    const QFileInfo fileInfo(fileName);
    switch (obj.position)
    {
    case 0u:
        ui->btOpenSynced->setText(fileInfo.fileName());
        break;

    case 1u:
        ui->btOpenDesynced->setText(fileInfo.fileName());
        break;

    default:
        break;
    }

    // Группировка
    GroupEvents(obj.script.events.content,
                obj.groups,
                ui->spinMinDuration->value(),
                ui->spinMaxOffset->value(),
                ui->chbSkipComments->isChecked(),
                ui->chbSkipLyrics->isChecked());

    this->drawGraph(obj);

    _openState |= 1u << obj.position;
    ui->btAutosync->setEnabled(0b11u == _openState);
}

void MainWindow::drawGraph(GraphStruct &obj, const DesyncGroupList * const highligted)
{
    if (obj.groups.isEmpty()) return;

    const QString tooltip = "%1 %2\n%3 → %4";
    const qreal HEIGHT = 50.0, SCALE = 0.002;
    QPen pen(obj.borderColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(true);
    const QBrush brushBg(obj.bgColor), brushHl(obj.hlColor);
    QTransform scale;
    scale.scale(1.0 / SCALE, 1.0);

    QVector<bool> hlMap;
    if (nullptr != highligted)
    {
        hlMap.fill(false, obj.groups.length());
        for (const DesyncGroup &dg : *highligted)
        {
            for (const int pos : dg.sync)
            {
                hlMap[pos] = true;
            }
        }
    }

    for (int i = 0, len = obj.groups.length(); i < len; ++i)
    {
        EventGroup &eg = obj.groups[i];

        GraphicsEventGroupItem * const item = new GraphicsEventGroupItem();
        item->rect()->setRect(0.0, 0.0, eg.end() - eg.start(), HEIGHT);
        item->rect()->setPen(pen);

        if (nullptr != highligted) item->rect()->setBrush(hlMap.at(i) ? brushHl : brushBg);
        else item->rect()->setBrush(brushBg);

        item->text()->setPlainText(QString::number(i + 1));
        item->text()->setTransform(scale);
        item->setPos(eg.start(), 0.0);
        item->setToolTip(tooltip
                         .arg(obj.title)
                         .arg(i + 1)
                         .arg(Script::Line::TimeToStr(eg.start(), Script::SCR_SRT))
                         .arg(Script::Line::TimeToStr(eg.end(), Script::SCR_SRT)));

        if (obj.connected)
        {
            item->setCursor(Qt::OpenHandCursor);
            item->setEventGroup(&eg);
            item->setMovable(true);
        }

        obj.items.append(item);
    }

    if (obj.connected)
    {
        for (int i = 0, len = obj.items.length(); i < len; ++i)
        {
            GraphicsEventGroupItem * const item = dynamic_cast<GraphicsEventGroupItem *>(obj.items.at(i));
            if (i > 0) item->setPrev( dynamic_cast<GraphicsEventGroupItem *>(obj.items.at(i - 1)) );
            if (i < len - 1) item->setNext( dynamic_cast<GraphicsEventGroupItem *>(obj.items.at(i + 1)) );
        }
    }

    if (ui->graphicsView->sceneRect().width() < obj.groups.last().end())
    {
        QRectF rect = ui->graphicsView->sceneRect();
        rect.setWidth(obj.groups.last().end());
        ui->graphicsView->setSceneRect(rect);
    }

    obj.graph = ui->graphicsView->scene()->createItemGroup(obj.items);
    obj.graph->setY((HEIGHT + 10) * obj.position);
    if (obj.connected) obj.graph->setHandlesChildEvents(false);
}

QString UrlToPath(const QUrl &url)
{
    const QString path = url.toLocalFile();

    if (!path.isEmpty() && FILETYPES.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive))
    {
        return path;
    }

    return QString::null;
}
