#pragma once

#include <QObject>
#include <QQuickView>
#include <QUrl>

#include <functional>
#include <memory>

class QQmlContext;

namespace sailfish
{
class PhonePresentation : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool open READ isOpen NOTIFY changed)

public:
  PhonePresentation(QUrl const & source, std::function<void(QQmlContext *)> setup, QObject * parent = nullptr);
  ~PhonePresentation() override;
  bool isOpen() const { return m_view != nullptr; }
  bool Show();
  void Close();

signals:
  void changed();

protected:
  bool eventFilter(QObject * watched, QEvent * event) override;

private:
  QUrl m_source;
  std::function<void(QQmlContext *)> m_setup;
  std::unique_ptr<QQuickView> m_view;
  bool m_closing = false;
};
}  // namespace sailfish
