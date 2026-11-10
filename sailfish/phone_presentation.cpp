#include "sailfish/phone_presentation.hpp"

#include "base/logging.hpp"

#include <QEvent>
#include <QQmlEngine>
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
    if (m_engine)
    {
      m_view = std::make_unique<QQuickView>(m_engine.get(), nullptr);
      m_view->setTitle(m_title);
      m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    }
    else
    {
      m_view.reset(SailfishApp::createView());
      // Silica's asynchronous component callbacks can outlive their original window.
      // Keep their engine while releasing and rebuilding the phone QML tree and view.
      m_engine.reset(m_view->engine());
      m_engine->setParent(nullptr);
      m_title = m_view->title();
    }
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
