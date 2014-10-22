#include "GLWidget.h"

#include <stdio.h>
#include <stdlib.h>
// #include <sys/time.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <ostream>
#include <fstream>
#include <streambuf>
#include <memory>

#include <QtGui/qapplication.h>
#include <QtGui/QDesktopWidget>
#include <QtGui/QLayout>
#include <QtGui/QVBoxLayout>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include "macros.h"

using namespace FabricSplice;

/////////////////////////////////////////////////////////////////////////////////////////////////////////
//  GLWidget
/////////////////////////////////////////////////////////////////////////////////////////////////////////

GLWidget::GLWidget(QGLFormat format, QWidget *parent) :
	QGLWidget(format, parent)
{	
  setAutoBufferSwap(false);

  m_fps = 0.0;

  for(int i=0;i<16;i++)
    m_fpsStack[i] = 0.0;

  m_fpsTimer.start();
  resetRTVals();
	setFocusPolicy(Qt::StrongFocus);

  m_redrawEnabled = false;
  m_painting = false;
  m_prevParent = NULL;
  m_fullScreenDialog = NULL;

  m_requiresInitialize = true;
  m_requiresResize = true;
  m_resizeEnabled = true;
  m_width = 0;
  m_height = 0;

}

GLWidget::~GLWidget()
{
  if(m_fullScreenDialog)
    delete(m_fullScreenDialog);
}

void GLWidget::resetRTVals()
{
  FABRIC_TRY("GLWidget::resetRTVals",

    m_drawing = constructObjectRTVal("OGLInlineDrawing");
    if(!m_drawing.isValid())
    {
      printf("[GLWidget] Error: Cannot construct OGLInlineDrawing RTVal (extension loaded?)\n");
      return;
    }
    m_drawing = m_drawing.callMethod("OGLInlineDrawing", "getInstance", 0, 0);

    m_viewport = constructObjectRTVal("SpliceStandaloneViewport");
    if(!m_viewport.isValid())
    {
      printf("[GLWidget] Error: Cannot construct SpliceStandaloneViewport RTVal (extension loaded?)\n");
      return;
    }
    else
    {
      std::vector<FabricCore::RTVal> args(2);
      args[0] = constructStringRTVal("default");
      args[1] = m_viewport;
      m_drawing.callMethod("", "registerViewport", args.size(), &args[0]);
    }

    m_camera = m_viewport.maybeGetMember("camera");
    m_viewport.setMember("windowId", constructUInt64RTVal((uint64_t)this->winId()));

    m_drawContext = constructObjectRTVal("DrawContext");
    if(!m_drawContext.isValid())
    {
      printf("[GLWidget] Error: Cannot construct DrawContext RTVal (extension loaded?)\n");
      return;
    }

  );

  m_requiresInitialize = true;
  m_requiresResize = true;
  m_resizeEnabled = true;
}

FabricCore::RTVal GLWidget::getInlineViewport()
{
  return m_viewport;
}

void GLWidget::paintGL()
{
  if(!m_redrawEnabled)
    return;
  if(m_painting)
    return;

  // compute the fps
  double ms = m_fpsTimer.elapsed();
  if(ms == 0.0)
    m_fps = 0.0;
  else
    m_fps = 1000.0 / ms;

  double averageFps = 0.0;
  for(int i=0;i<15;i++) {
    m_fpsStack[i+1] = m_fpsStack[i];
    averageFps += m_fpsStack[i];
  }
  m_fpsStack[0] = m_fps;
  averageFps += m_fps;
  averageFps /= 16.0;
  m_fps = averageFps;

  m_fpsTimer.start();

	// perform drawing
  if(m_requiresInitialize)
  {
    FABRIC_TRY("GLWidget::initializeGL",

      m_viewport.callMethod("", "setup", 1, &m_drawContext);

    );
    m_requiresInitialize = false;
    m_requiresResize = true;
  }

  if(m_requiresResize)
  {
    FABRIC_TRY("GLWidget::resizeGL",

      std::vector<FabricCore::RTVal> args(3);
      args[0] = m_drawContext;
      args[1] = constructUInt32RTVal(m_width);
      args[2] = constructUInt32RTVal(m_height);
      m_viewport.callMethod("", "resize", args.size(), &args[0]);

    );
    m_requiresResize = false;
    m_resizeEnabled = true;
  }

  m_painting = true;

  bool responsibleForSwappingBuffers = false;

  FABRIC_TRY("GLWidget::paintGL",

    std::vector<FabricCore::RTVal> args(2);
    args[0] = constructStringRTVal("default");
    args[1] = m_drawContext;
    m_drawing.callMethod("", "drawViewport", args.size(), &args[0]);

    FabricCore::RTVal responsibleForSwappingBuffersVal = m_viewport.maybeGetMember("responsibleForSwappingBuffers");
    responsibleForSwappingBuffers = responsibleForSwappingBuffersVal.getBoolean();
  );

  if(!responsibleForSwappingBuffers)
    swapBuffers();

  emit redrawn();

  m_painting = false;
}

void GLWidget::setTime(float time)
{
  FABRIC_TRY("GLWidget::setTime",

    FabricCore::RTVal timeVal = constructFloat32RTVal(time);
    m_drawContext.setMember("time", timeVal);

  );
  updateGL();
}


void GLWidget::setWireFrame(bool wireFrame)
{
  FABRIC_TRY("GLWidget::setWireFrame",

    FabricCore::RTVal wireFrameVal = constructBooleanRTVal(wireFrame);
    m_viewport.callMethod("", "setWireFrame", 1, &wireFrameVal);

  );
  updateGL();
}

void GLWidget::toggleGrid()
{
  FABRIC_TRY("GLWidget::toggleGrid",

    m_viewport.callMethod("", "toggleGrid", 0, 0);

  );
  updateGL();
}

/*************************************************************************/
/* QGLWidget calls                                                       */
/*************************************************************************/

void GLWidget::getArgsForMouseEvent(QMouseEvent * event, std::vector<FabricCore::RTVal> & args)
{
  args.resize(4);
  args[0] = constructSInt32RTVal(event->pos().x());
  args[1] = constructSInt32RTVal(event->pos().y());
  int buttons = 0;
  if(event->buttons().testFlag(Qt::LeftButton))
    buttons += 1;
  if(event->buttons().testFlag(Qt::RightButton))
    buttons += 2;
  if(event->buttons().testFlag(Qt::MidButton))
    buttons += 4;
  args[2] = constructSInt32RTVal(buttons);
  int modifiers = 0;
  if(qApp->keyboardModifiers().testFlag(Qt::AltModifier))
    modifiers += 1;
  if(qApp->keyboardModifiers().testFlag(Qt::ShiftModifier))
    modifiers += 2;
  if(qApp->keyboardModifiers().testFlag(Qt::ControlModifier))
    modifiers += 4;
  args[3] = constructSInt32RTVal(modifiers);
}

void GLWidget::mousePressEvent(QMouseEvent *event)
{

  FABRIC_TRY("GLWidget::mousePressEvent",

    // todo: phtaylor
    // std::vector<FabricCore::RTVal> args;
    // getArgsForMouseEvent(event, args);
    // m_viewport.callMethod("", "mousePressEvent", args.size(), &args[0]);

  );
  updateGL();
}

void GLWidget::mouseMoveEvent(QMouseEvent *event)
{
  FABRIC_TRY("GLWidget::mouseMoveEvent",

    // todo: phtaylor
    // std::vector<FabricCore::RTVal> args;
    // getArgsForMouseEvent(event, args);
    // m_viewport.callMethod("", "mouseMoveEvent", args.size(), &args[0]);

  );
  updateGL();
}

void GLWidget::mouseReleaseEvent(QMouseEvent *event)
{
  FABRIC_TRY("GLWidget::mouseReleaseEvent",

    // todo: phtaylor
    // std::vector<FabricCore::RTVal> args;
    // getArgsForMouseEvent(event, args);
    // m_viewport.callMethod("", "mouseReleaseEvent", args.size(), &args[0]);

  );
  updateGL();
}

void GLWidget::wheelEvent(QWheelEvent *event)
{
  FABRIC_TRY("GLWidget::wheelEvent",

    // todo: phtaylor
    // std::vector<FabricCore::RTVal> args(1);
    // args[0] = constructSInt32RTVal(event->delta());
    // m_viewport.callMethod("", "wheelEvent", args.size(), &args[0]);

  );
	updateGL();
}

void GLWidget::initializeGL()
{
  m_requiresInitialize = true;
}

void GLWidget::resizeGL(int width, int height)
{
  if(!m_resizeEnabled)
    return;
  if(m_painting)
    return;
  if(isGLFullScreen())
    return;

  m_width = width;
  m_height = height;

  m_painting = false;
  m_requiresResize = true;
}

void GLWidget::toggleGLFullScreen()
{
  if(m_fullScreenDialog)
  {
    m_resizeEnabled = false;

    setParent(m_prevParent);
    m_prevParent->layout()->addWidget(this);
    m_fullScreenDialog->close();

    delete(m_fullScreenDialog);
    m_fullScreenDialog = NULL;

    m_requiresInitialize = true;

    m_width = m_prevParent->width();
    m_height = m_prevParent->height();

    m_prevParent = NULL;

    updateGL();
  }
  else
  {
    m_resizeEnabled = false;

    m_prevParent = parentWidget();
    m_fullScreenDialog = new QDialog();
    m_fullScreenDialog->setLayout(new QVBoxLayout());
    m_fullScreenDialog->layout()->setContentsMargins(0, 0, 0, 0);
    setParent(m_fullScreenDialog);
    m_fullScreenDialog->layout()->addWidget(this);
    m_fullScreenDialog->setModal(false);
    m_fullScreenDialog->setWindowFlags(Qt::SplashScreen);
    m_fullScreenDialog->resize(QApplication::desktop()->width(), QApplication::desktop()->height());

    m_width = QApplication::desktop()->width();
    m_height = QApplication::desktop()->height();

    m_requiresInitialize = true;
    m_fullScreenDialog->show();
    updateGL();
  }        
}

void GLWidget::walk(float x, float y, float z) {
  FABRIC_TRY("GLWidget::walk",

    // todo: phtaylor
    // std::vector<FabricCore::RTVal> args(3);
    // args[0] = constructFloat32RTVal(x);
    // args[1] = constructFloat32RTVal(y);
    // args[2] = constructFloat32RTVal(z);
    // m_camera.callMethod("", "walk", args.size(), &args[0]);

  );
  updateGL();
}

void GLWidget::turn(float x, float y) {
  FABRIC_TRY("GLWidget::turn",

    // todo: phtaylor
    // std::vector<FabricCore::RTVal> args(2);
    // args[0] = constructFloat32RTVal(x);
    // args[1] = constructFloat32RTVal(y);
    // m_camera.callMethod("", "turn", args.size(), &args[0]);

  );
  updateGL();
}

void GLWidget::addFilePath(const std::string & filePath) {
  FABRIC_TRY("GLWidget::addFilePath",

    FabricCore::RTVal filePathVal = constructStringRTVal(filePath.c_str());
    m_viewport.callMethod("", "addFilePath", 1, &filePathVal);

  );
}
