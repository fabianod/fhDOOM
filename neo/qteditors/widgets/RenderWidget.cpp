#include "RenderWidget.h"
#include "qwindow.h"
#include "qopenglcontext.h"
#include <QMouseEvent>
#include <QtPlatformHeaders/QWGLNativeContext>
#include "../tools/radiant/QE3.H"
#include "../tools/radiant/GLWidget.h"
#include "../../sys/win32/win_local.h"
#include "../../tools/radiant/GLWidget.h"
#include "../../renderer/tr_local.h"


class fhRenderWindow : public QWindow {
public:
	fhRenderWindow( idGLDrawable** drawable, QWindow* parent = nullptr )
		: QWindow( parent )
		, m_context( nullptr )
		, m_initialized( false )
		, m_drawable( drawable ) {

		setSurfaceType( QWindow::OpenGLSurface );
	}

	virtual void exposeEvent( QExposeEvent * ) override {
		if (isExposed())
			render();
	}

	virtual void mouseMoveEvent(QMouseEvent * ev) override {
		if(ev->type() == QEvent::MouseMove) {
			(*m_drawable)->mouseMove(ev->x(), ev->y());
		}
	}

	virtual bool event(QEvent* ev) override {
		switch(ev->type()) {
		case QEvent::UpdateRequest:
			render();
			break;
		case QEvent::MouseButtonPress:
			handleButton(dynamic_cast<QMouseEvent*>(ev));
			break;
		case QEvent::MouseButtonRelease:
			handleButton(dynamic_cast<QMouseEvent*>(ev));
			break;
		};
		return QWindow::event(ev);
	}

	void handleButton(QMouseEvent* ev) {
		if(!ev)
			return;

		if (!m_drawable || !*m_drawable)
			return;

		if (ev->type() == QEvent::MouseButtonPress) {
			if (ev->button() == Qt::LeftButton)
				(*m_drawable)->buttonDown( 1, ev->x(), ev->y() );

			if (ev->button() == Qt::RightButton)
				(*m_drawable)->buttonDown( 2, ev->x(), ev->y() );
		}

		if (ev->type() == QEvent::MouseButtonRelease) {
			if (ev->button() == Qt::LeftButton)
				(*m_drawable)->buttonUp( 1, ev->x(), ev->y() );

			if (ev->button() == Qt::RightButton)
				(*m_drawable)->buttonUp( 2, ev->x(), ev->y() );
		}
	}

	void render() {
		if (!m_initialized) {
			init();
			m_initialized = true;
		}

		if (!m_context)
			return;

		if (!m_context->makeCurrent( this ))
			return;

		QSize rect = size();

		glViewport( 0, 0, rect.width(), rect.height() );
		glScissor( 0, 0, rect.width(), rect.height() );

		GL_ProjectionMatrix.LoadIdentity();
		GL_ProjectionMatrix.Ortho( 0, rect.width(), 0, rect.height(), -256, 256 );

		glClearColor( 0.4f, 0.4f, 0.4f, 0.7f );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

		glDisable( GL_DEPTH_TEST );
		glDisable( GL_BLEND );

		if (m_drawable && *m_drawable) {
			(*m_drawable)->draw( 1, 1, rect.width() - 1, rect.height() - 1 );
		}

		m_context->swapBuffers( this );
		wglMakeCurrent(win32.hDC, win32.hGLRC);		
	}

private:
	void init() {
		auto ctx = win32.hGLRC;
		auto hwnd = (HWND)this->winId();
		auto hDC = GetDC( hwnd );

		int pixelFormat = ChoosePixelFormat( hDC, &win32.pfd );
		if (pixelFormat > 0) {
			if (SetPixelFormat( hDC, pixelFormat, &win32.pfd ) == NULL) {
				int foo = 2;
			}
		}

		ReleaseDC( hwnd, hDC );

		m_context = new QOpenGLContext( this );
		//		m_context->setFormat( requestedFormat() );
		QWGLNativeContext nativeContext( ctx, hwnd );
		m_context->setNativeHandle( QVariant::fromValue( nativeContext ) );

		if (!m_context->create()) {
			delete m_context;
			m_context = 0;
		}
	}

	QOpenGLContext* m_context;
	bool            m_initialized;
	idGLDrawable**   m_drawable;
};


fhRenderWidget::fhRenderWidget(QWidget* parent)
: QWidget(parent)
, m_drawable(nullptr)
{
	auto layout = new QHBoxLayout;
	this->setLayout(layout);
	m_window = new fhRenderWindow(&m_drawable);
	layout->addWidget( QWidget::createWindowContainer(m_window, this));
}

fhRenderWidget::~fhRenderWidget() {
}

void fhRenderWidget::updateDrawable() {
	m_window->requestUpdate();
}
