/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "plotwidget_editor.h"
#include "ui_plotwidget_editor.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include <QListWidgetItem>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QDialogButtonBox>
#include "qwt_text.h"
#include <QTimer>

const double MAX_DOUBLE = std::numeric_limits<double>::max() / 2;

namespace bugfix
{
QListWidgetDragMovement::QListWidgetDragMovement(QWidget * parent)
  : QListWidget(parent)
{
}

QListWidgetDragMovement::~QListWidgetDragMovement()
{
}

void QListWidgetDragMovement::dragMoveEvent(QDragMoveEvent *e)
{
    if ((row(itemAt(e->pos())) == currentRow() + 1) 
        || (currentRow() == count() - 1 && row(itemAt(e->pos())) == -1)) {
        e->ignore();
    }
    else {
        QListWidget::dragMoveEvent(e);
    }
}
}

PlotwidgetEditor::PlotwidgetEditor(PlotWidget* plotwidget, QWidget* parent)
  : QDialog(parent), ui(new Ui::PlotWidgetEditor), _plotwidget_origin(plotwidget)
{
  ui->setupUi(this);
  _list_widget = new bugfix::QListWidgetDragMovement();
  ui->verticalLayout_3->addWidget(_list_widget);
  installEventFilter(this);

  //  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

  setupColorWidget();

  QDomDocument doc;
  auto saved_state = plotwidget->xmlSaveState(doc);

  _plotwidget = new PlotWidget(plotwidget->datamap(), this);
  _plotwidget->xmlLoadState(saved_state);
  _plotwidget->on_changeTimeOffset(plotwidget->timeOffset());
  _plotwidget->setContextMenuEnabled(false);

  _bounding_rect_original = _plotwidget_origin->currentBoundingRect();

  auto layout = new QVBoxLayout();
  ui->framePlotPreview->setLayout(layout);
  layout->addWidget(_plotwidget);
  layout->setMargin(6);

  _plotwidget->zoomOut(false);

  setupTable();

  QSettings settings;
  restoreGeometry(settings.value("PlotwidgetEditor.geometry").toByteArray());

  ui->lineLimitMax->setValidator(new QDoubleValidator(this));
  ui->lineLimitMin->setValidator(new QDoubleValidator(this));

  auto ylimits = _plotwidget->customAxisLimit();
  auto range_x = _plotwidget->getVisualizationRangeX();
  Range suggested_limits = _plotwidget->getVisualizationRangeY(range_x);

  if (ylimits.min != -MAX_DOUBLE)
  {
    ui->checkBoxMin->setChecked(true);
    ui->lineLimitMin->setText(QString::number(ylimits.min));
  }
  else
  {
    ui->lineLimitMin->setText(QString::number(suggested_limits.min));
  }

  if (ylimits.max != MAX_DOUBLE)
  {
    ui->checkBoxMax->setChecked(true);
    ui->lineLimitMax->setText(QString::number(ylimits.max));
  }
  else
  {
    ui->lineLimitMax->setText(QString::number(suggested_limits.max));
  }

  // _list_widget->widget_background_disabled("QListView::item:selected { background:
  // #ddeeff; }"); 
  
  _list_widget->setDragDropMode(QAbstractItemView::InternalMove);
  _list_widget->setSelectionMode(QAbstractItemView::SingleSelection);
  _list_widget->setDragEnabled(true);
  _list_widget->setAcceptDrops(true);
  _list_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  _list_widget->setSelectionBehavior(QAbstractItemView::SelectRows);
  connect(_list_widget->model(), &QAbstractItemModel::rowsMoved, this, &PlotwidgetEditor::onRowsMoved);
  connect(_list_widget, &QListWidget::itemSelectionChanged, this, &PlotwidgetEditor::listWidgetItemSelectionChanged);

  if (_list_widget->count() != 0)
  {
    _list_widget->item(0)->setSelected(true);

    auto item = _list_widget->item(0);
    auto itemWidget = _list_widget->itemWidget(item);
    auto row_widget = dynamic_cast<EditorRowWidget*>(itemWidget);
    if(row_widget)
    { 
      updateRadioButtonsFromCurveStyle(row_widget->text());
    }
  }
}

PlotwidgetEditor::~PlotwidgetEditor()
{
  QSettings settings;
  settings.setValue("PlotwidgetEditor.geometry", saveGeometry());

  delete _plotwidget;
  delete ui;
}

void PlotwidgetEditor::onColorChanged(QColor c)
{
  auto selected = _list_widget->selectedItems();
  if (selected.size() != 1)
  {
    return;
  }
  auto item = selected.front();
  if (item)
  {
    auto row_widget = dynamic_cast<EditorRowWidget*>(_list_widget->itemWidget(item));
    auto name = row_widget->text();
    if (row_widget->color() != c)
    {
      row_widget->setColor(c);
    }

    _plotwidget->on_changeCurveColor(item->data(Qt::UserRole).toString(), c);
  }
}

void PlotwidgetEditor::onRowsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row)
{
  int sourceIndex = start;
  int destinationIndex = row;

  QDomDocument doc;
  _plotwidget->changeCurvePositionInList(sourceIndex, destinationIndex);
  auto saved_state = _plotwidget->xmlSaveState(doc);
  _plotwidget->xmlLoadState(saved_state);

}

void PlotwidgetEditor::setupColorWidget()
{
  auto wheel_layout = new QVBoxLayout();
  wheel_layout->setMargin(0);
  wheel_layout->setSpacing(5);
  ui->widgetWheel->setLayout(wheel_layout);
  _color_wheel = new color_widgets::ColorWheel(this);
  wheel_layout->addWidget(_color_wheel);

  _color_preview = new color_widgets::ColorPreview(this);
  _color_preview->setMaximumHeight(25);

  wheel_layout->addWidget(_color_preview);

  connect(_color_wheel, &color_widgets::ColorWheel::colorChanged, this,
          &PlotwidgetEditor::onColorChanged);

  connect(_color_wheel, &color_widgets::ColorWheel::colorChanged, _color_preview,
          &color_widgets::ColorPreview::setColor);

  connect(_color_wheel, &color_widgets::ColorWheel::colorChanged, this,
          [this](QColor col) {
            QSignalBlocker block(ui->editColotText);
            ui->editColotText->setText(col.name());
          });

  _color_wheel->setColor(Qt::blue);
}

void PlotwidgetEditor::onDeleteRow(QWidget* w)
{
  int row_count = _list_widget->count();
  for (int row = 0; row < row_count; row++)
  {
    auto item = _list_widget->item(row);
    auto widget = _list_widget->itemWidget(item);
    if (widget == w)
    {
      QString curve = dynamic_cast<EditorRowWidget*>(w)->text();

      _list_widget->takeItem(row);
      _plotwidget->removeCurve(curve);
      widget->deleteLater();
      row_count--;
      break;
    }
  }
  if (row_count == 0)
  {
    disableWidgets();
  }
  _plotwidget->replot();
}

void PlotwidgetEditor::disableWidgets()
{
  ui->widgetColor->setEnabled(false);
  _color_wheel->setEnabled(false);
  _color_preview->setEnabled(false);

  ui->frameLimits->setEnabled(false);
  ui->frameStyle->setEnabled(false);
  ui->pushButtonApplyToAll->setEnabled(false);
}

void PlotwidgetEditor::setupTable()
{
  std::map<QString, QColor> colors = _plotwidget->getCurveColors();

  int row = 0;
  for (auto& it : _plotwidget->curveList())
  {
    auto curve_name = QString::fromStdString(it.src_name);
    auto color = colors.at(curve_name);
    auto item = new QListWidgetItem();
    // even if it is not visible, we store here the original name (not curve_name)
    item->setData(Qt::UserRole, curve_name);

    _list_widget->addItem(item);
    auto plot_row = new EditorRowWidget(curve_name, color);
    item->setSizeHint(plot_row->sizeHint());
    _list_widget->setItemWidget(item, plot_row);

    connect(plot_row, &EditorRowWidget::deleteRow, this,
            [this](QWidget* w) { onDeleteRow(w); });
    row++;
  }
  if (row == 0)
  {
    disableWidgets();
  }
}

void PlotwidgetEditor::updateLimits()
{
  double ymin = -MAX_DOUBLE;
  double ymax = MAX_DOUBLE;

  if (ui->checkBoxMax->isChecked() && !ui->lineLimitMax->text().isEmpty())
  {
    bool ok = false;
    double val = ui->lineLimitMax->text().toDouble(&ok);
    if (ok)
    {
      ymax = val;
    }
  }

  if (ui->checkBoxMin->isChecked() && !ui->lineLimitMin->text().isEmpty())
  {
    bool ok = false;
    double val = ui->lineLimitMin->text().toDouble(&ok);
    if (ok)
    {
      ymin = val;
    }
  }

  if (ymin > ymax)
  {
    // swap
    ui->lineLimitMin->setText(QString::number(ymax));
    ui->lineLimitMax->setText(QString::number(ymin));
    std::swap(ymin, ymax);
  }

  Range range;
  range.min = ymin;
  range.max = ymax;
  _plotwidget->setCustomAxisLimits(range);
}

void PlotwidgetEditor::on_editColotText_textChanged(const QString& text)
{
  if (text.size() == 7 && text[0] == '#' && QColor::isValidColor(text))
  {
    QColor col(text);
    _color_wheel->setColor(col);
    _color_preview->setColor(col);
  }
}

void PlotwidgetEditor::on_radioLines_toggled(bool checked)
{
  if (checked)
  {
    updateSelectedCurvesStyle(PlotWidgetBase::LINES);
  }
}

void PlotwidgetEditor::on_radioPoints_toggled(bool checked)
{
  if (checked)
  {
    updateSelectedCurvesStyle(PlotWidgetBase::DOTS);
  }
}

void PlotwidgetEditor::on_radioBoth_toggled(bool checked)
{
  if (checked)
  {
    updateSelectedCurvesStyle(PlotWidgetBase::LINES_AND_DOTS);
  }
}

void PlotwidgetEditor::on_radioSticks_toggled(bool checked)
{
  if (checked)
  {
    updateSelectedCurvesStyle(PlotWidgetBase::STICKS);
  }
}

void PlotwidgetEditor::on_radioSteps_toggled(bool checked)
{
  if (checked)
  {
    updateSelectedCurvesStyle(PlotWidgetBase::STEPS);
  }
}

void PlotwidgetEditor::on_radioStepsInv_toggled(bool checked)
{
  if (checked)
  {
    updateSelectedCurvesStyle(PlotWidgetBase::STEPSINV);
  }
}

void PlotwidgetEditor::on_checkBoxMax_toggled(bool checked)
{
  ui->lineLimitMax->setEnabled(checked);
  updateLimits();
}

void PlotwidgetEditor::on_checkBoxMin_toggled(bool checked)
{
  ui->lineLimitMin->setEnabled(checked);
  updateLimits();
}

void PlotwidgetEditor::on_pushButtonReset_clicked()
{
  Range no_limits;
  no_limits.min = -MAX_DOUBLE;
  no_limits.max = +MAX_DOUBLE;

  _plotwidget->setCustomAxisLimits(no_limits);

  auto range_x = _plotwidget->getVisualizationRangeX();
  Range limits = _plotwidget->getVisualizationRangeY(range_x);

  ui->lineLimitMin->setText(QString::number(limits.min));
  ui->lineLimitMax->setText(QString::number(limits.max));
}

void PlotwidgetEditor::on_pushButtonApplyToAll_clicked()
{
  auto selected_items = _list_widget->selectedItems();
  if (selected_items.size() != 1 || _list_widget->count() == 0)
  {
    return;
  }

  auto item = selected_items.front();
  auto itemWidget = _list_widget->itemWidget(item);
  auto row_widget = dynamic_cast<EditorRowWidget*>(_list_widget->itemWidget(item));
  if(row_widget)
  { 
    QString curve_title = row_widget->text();
    auto curve_style = _plotwidget->curveStyle(curve_title);
    _plotwidget->changeAllCurvesStyle(curve_style);
  }
}

void PlotwidgetEditor::on_lineLimitMax_textChanged(const QString&)
{
  updateLimits();
}

void PlotwidgetEditor::on_lineLimitMin_textChanged(const QString&)
{
  updateLimits();
}

void PlotwidgetEditor::on_pushButtonCancel_pressed()
{
  this->reject();
}

void PlotwidgetEditor::on_pushButtonSave_pressed()
{
  QDomDocument doc;
  _plotwidget->setZoomRectangle(_bounding_rect_original, false);
  auto elem = _plotwidget->xmlSaveState(doc);
  _plotwidget_origin->xmlLoadState(elem);
  this->accept();
}

EditorRowWidget::EditorRowWidget(QString text, QColor color) : QWidget()
{
  setMouseTracking(true);
  const QSize button_size(20, 20);

  auto layout = new QHBoxLayout();
  setLayout(layout);
  _text = new QLabel(text, this);

  _empty_spacer = new QWidget();
  _empty_spacer->setFixedSize(button_size);

  setColor(color);
  _delete_button = new QPushButton(this);
  _delete_button->setFlat(true);
  _delete_button->setFixedSize(button_size);
  auto icon = QIcon(":/resources/svg/trash.svg");
  _delete_button->setStyleSheet("QPushButton:hover{ border: 0px;}");

  _delete_button->setIcon(icon);
  _delete_button->setIconSize(button_size);

  layout->addWidget(_empty_spacer);
  layout->addWidget(_delete_button);
  layout->addWidget(_text);

  _delete_button->setHidden(true);

  connect(_delete_button, &QPushButton::clicked, this,
          [this]() { emit deleteRow(this); });
}

void EditorRowWidget::enterEvent(QEvent* ev)
{
  _delete_button->setHidden(false);
  _empty_spacer->setHidden(true);
}

void EditorRowWidget::leaveEvent(QEvent* ev)
{
  _delete_button->setHidden(true);
  _empty_spacer->setHidden(false);
}

QString EditorRowWidget::text() const
{
  return _text->text();
}

void EditorRowWidget::setColor(QColor color)
{
  setStyleSheet(QString("color: %1;").arg(color.name()));
  _color = color;
}

QColor EditorRowWidget::color() const
{
  return _color;
}

void PlotwidgetEditor::listWidgetItemSelectionChanged()
{
  auto selected = _list_widget->selectedItems();
  if (selected.size() == 0 || _list_widget->count() == 0)
  {
    ui->widgetColor->setEnabled(false);
    ui->editColotText->setText("#000000");
    return;
  }

  ui->widgetColor->setEnabled(true);

  if (selected.size() != 1)
  {
    return;
  }

  auto item = selected.front();
  auto row_widget = dynamic_cast<EditorRowWidget*>(_list_widget->itemWidget(item));
  if (row_widget)
  {
    ui->editColotText->setText(row_widget->color().name());
    updateRadioButtonsFromCurveStyle(row_widget->text());
  }
}

void PlotwidgetEditor::updateRadioButtonsFromCurveStyle(const QString& curve_title)
{
  if (_plotwidget->curveStyle(curve_title) == PlotWidgetBase::LINES)
  {
    ui->radioLines->setChecked(true);
  }
  else if (_plotwidget->curveStyle(curve_title) == PlotWidgetBase::DOTS)
  {
    ui->radioPoints->setChecked(true);
  }
  else if (_plotwidget->curveStyle(curve_title) == PlotWidgetBase::STICKS)
  {
    ui->radioSticks->setChecked(true);
  }
  else if (_plotwidget->curveStyle(curve_title) == PlotWidgetBase::STEPS)
  {
    ui->radioSteps->setChecked(true);
  }
  else if (_plotwidget->curveStyle(curve_title) == PlotWidgetBase::STEPSINV)
  {
    ui->radioStepsInv->setChecked(true);
  }
  else
  {
    ui->radioBoth->setChecked(true);
  }
}

void PlotwidgetEditor::updateSelectedCurvesStyle(PlotWidgetBase::CurveStyle style)
{
  auto selected_items = _list_widget->selectedItems();
  if (selected_items.size() != 1 || _list_widget->count() == 0)
  {
    return;
  }

  auto item = selected_items.front();
  auto itemWidget = _list_widget->itemWidget(item);
  auto row_widget = dynamic_cast<EditorRowWidget*>(_list_widget->itemWidget(item));
  if(row_widget)
  { 
    QString curve_title = row_widget->text();
    _plotwidget->changeCurveStyle(curve_title, style);
  }
}


