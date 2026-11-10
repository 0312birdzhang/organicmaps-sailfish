#include "sailfish/phone_presentation.hpp"

#include "base/logging.hpp"

#include <QEvent>
#include <QQmlError>
#include <QTimer>

#include <utility>

#include <sailfishapp.h>

namespace sailfish
{
PhonePresentation::PhonePresentation(QUrl const & source, std::function<void(QQmlContext *)> setup, QObject * parent)
  : QObject(parent)
  , m_source(source)
  , m_setup(std::move(setup))
{}

PhonePresentation::~PhonePresentation()
{
  Close();
}

bool PhonePresentation::Show()
{
  if (m_closing)
    Close();
  if (!m_view)
  {
    m_view.reset(SailfishApp::createView());
    m_setup(m_view->rootContext());
    m_view->setSource(m_source);
    if (m_view->status() != QQuickView::Ready)
    {
      for (auto const & error : m_view->errors())
        LOG(LERROR, (error.toString().toStdString()));
      m_view.reset();
      return false;
    }
    m_view->installEventFilter(this);
    emit changed();
  }
  m_view->show();
  m_view->requestActivate();
  return true;
}

void PhonePresentation::Close()
{
  m_closing = false;
  if (!m_view)
    return;
  m_view->removeEventFilter(this);
  m_view.reset();
  emit changed();
}

bool PhonePresentation::eventFilter(QObject * watched, QEvent * event)
{
  if (watched == m_view.get() && event->type() == QEvent::Close && !m_closing)
  {
    m_closing = true;
    QTimer::singleShot(0, this, [this]
    {
      if (m_closing)
        Close();
    });
  }
  return QObject::eventFilter(watched, event);
}
}  // namespace sailfish
