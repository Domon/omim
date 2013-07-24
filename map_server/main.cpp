#include "main.hpp"
#include "render_context.hpp"

#include "../graphics/resource_manager.hpp"

#include "../indexer/mercator.hpp"

#include "../map/render_policy.hpp"
#include "../map/simple_render_policy.hpp"
#include "../map/framework.hpp"

#include "../gui/controller.hpp"

#include "../platform/platform.hpp"
#include "../platform/settings.hpp"

#include "../std/shared_ptr.hpp"

#include "qjsonrpcservice.h"

#include <QGLPixelBuffer>
#include <QtCore/QBuffer>
#include <QtGui/QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QLocalServer>
#include <QFile>
#include <QDir>


MwmRpcService::MwmRpcService(QObject *parent)
{
  LOG(LINFO, ("MwmRpcService started"));
  m_pixelBuffer = shared_ptr<QGLPixelBuffer>(new QGLPixelBuffer(2048, 2048));

  m_pixelBuffer->makeCurrent();
  shared_ptr<srv::RenderContext> primaryRC(new srv::RenderContext());
  graphics::ResourceManager::Params rmParams;
  rmParams.m_rtFormat = graphics::Data8Bpp;
  rmParams.m_texFormat = graphics::Data8Bpp;
  rmParams.m_texRtFormat = graphics::Data4Bpp;
  rmParams.m_videoMemoryLimit = GetPlatform().VideoMemoryLimit();

  RenderPolicy::Params rpParams;

  shared_ptr<VideoTimer> timer(new EmptyVideoTimer());

  rpParams.m_videoTimer = timer.get();
  rpParams.m_useDefaultFB = true;
  rpParams.m_rmParams = rmParams;
  rpParams.m_primaryRC = primaryRC;
  rpParams.m_density = graphics::EDensityXHDPI;
  rpParams.m_skinName = "basic.skn";
  rpParams.m_screenWidth = 2048;
  rpParams.m_screenHeight = 2048;

  try
  {
    m_framework.SetRenderPolicy(new SimpleRenderPolicy(rpParams));
    m_framework.GetGuiController()->ResetRenderParams();
  }
  catch (graphics::gl::platform_unsupported const & e)
  {
    LOG(LERROR, ("OpenGL platform is unsupported, reason: ", e.what()));
  }

}

QString MwmRpcService::RenderBox(
    const QVariant bbox,
    int width,
    int height,
    const QString &density,
    const QString &language,
    bool maxScaleMode
    )
{
  LOG(LINFO, ("Render box started", width, height, maxScaleMode));

  // @todo: set density and language from parameter
  // Settings::SetCurrentLanguage(string(language.toAscii()));
  // graphics::convert(density.toAscii(), rpParams.m_density);

  m_pixelBuffer->makeCurrent();
  m_framework.OnSize(width, height);
  m_framework.SetQueryMaxScaleMode(maxScaleMode);

  QVariantList box(bbox.value<QVariantList>());
  m2::AnyRectD requestBox(m2::RectD(
                            MercatorBounds::LonToX(box[0].toDouble()),
                            MercatorBounds::LatToY(box[1].toDouble()),
                            MercatorBounds::LonToX(box[2].toDouble()),
                            MercatorBounds::LatToY(box[3].toDouble())
                         ));
  m_framework.GetNavigator().SetFromRect(requestBox);

  shared_ptr<PaintEvent> pe(new PaintEvent(m_framework.GetRenderPolicy()->GetDrawer().get()));

  m_framework.BeginPaint(pe);
  m_framework.DoPaint(pe);
  m_framework.EndPaint(pe);

  QByteArray ba;
  QBuffer b(&ba);
  b.open(QIODevice::WriteOnly);

  m_pixelBuffer->toImage().copy(0, m_pixelBuffer->height()-height, width, height).save(&b, "PNG");

  LOG(LINFO, ("Render box finished"));
  return QString(ba.toBase64());
}

bool MwmRpcService::Ping()
{
  return true;
}

void MwmRpcService::Exit()
{
  qApp->exit(0);
}

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  string socketPath;

  if (!Settings::Get("ServerSocketPath", socketPath))
  {
    socketPath = "/tmp/mwm-render-socket";
    Settings::Set("ServerSocketPath", socketPath);
  }

  QString qSocketPath(socketPath.c_str());

  if (QFile::exists(qSocketPath))
  {
    if (!QFile::remove(qSocketPath))
    {
      qDebug() << "couldn't delete temporary service";
      return -1;
    }
  }

  MwmRpcService service;
  QJsonRpcLocalServer rpcServer;
  rpcServer.addService(&service);
  if (!rpcServer.listen(qSocketPath))
  {
    qDebug() << "could not start server: " << rpcServer.errorString();
    return -1;
  }

  return app.exec();
}
