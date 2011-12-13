#include <cmath>
#include "RenderThread.h"
#include <QTime>
#include <QDebug>


RenderThread::RenderThread(ExposureStack * es, float gamma, QObject * parent)
	: QThread(parent), restart(false), abort(false), images(es), minx(0), miny(0), maxx(0), maxy(0), scale(0) {
	setGamma(gamma);
}


RenderThread::~RenderThread() {
	abort = true;
	condition.wakeOne();
	wait();
	delete images;
}


void RenderThread::setGamma(float g) {
	mutex.lock();
	g = 1.0f / g;
	for (int i = 0; i < 65536; i++)
		gamma[i] = (int)std::floor(65536.0f * std::pow(i / 65536.0f, g)) >> 8;
	mutex.unlock();
}


void RenderThread::setExposureThreshold(int i, int th) {
	mutex.lock();
	images->setThreshold(i, ((th + 1) << 8) - 1);
	restart = true;
	mutex.unlock();
	condition.wakeOne();
}


void RenderThread::setExposureRelativeEV(int i, double re) {
	mutex.lock();
	images->setRelativeExposure(i, re);
	restart = true;
	mutex.unlock();
	condition.wakeOne();
}


void RenderThread::calculateWB(int x, int y, int radius) {
	mutex.lock();
	unsigned int w = images->getWidth() - x > 2*radius ? 2*radius : images->getWidth() - x;
	unsigned int h = images->getHeight() - y > 2*radius ? 2*radius : images->getHeight() - y;
	x = x > radius ? x - radius : 0;
	y = y > radius ? y - radius : 0;
	images->calculateWB(x, y, w, h);
	restart = true;
	emit whiteBalanceChanged(images->getWBGR(), images->getWBBR());
	mutex.unlock();
	condition.wakeOne();
}


void RenderThread::setImageViewport(int x, int y, int w, int h, int newScale) {
	mutex.lock();
	if (newScale != scale) {
		restart = true;
		scale = newScale;
	}
	minx = x;
	miny = y;
	maxx = x + w;
	maxy = y + h;
	qDebug() << "Viewport set to " << minx << ',' << miny << ':' << maxx << ',' << maxy ;
	mutex.unlock();
	if (restart)
		condition.wakeOne();
}


void RenderThread::doRender(unsigned int minx, unsigned int miny, unsigned int maxx, unsigned int maxy, QImage & image) {
	QTime t;
	t.start();
	// Iterate through pixels
	for (unsigned int row = miny; !restart && row < maxy; row++) {
		if (abort) return;

		QRgb * scanLine = reinterpret_cast<QRgb *>(image.scanLine(row - miny));
		for (unsigned int col = minx; col < maxx; col++) {
			double rr, gg, bb;
			images->rgb(col, row, rr, gg, bb);
			int r = (int)rr, g = (int)gg, b = (int)bb;
			if (r >= 65536 || r < 0) std::cerr << "RValue " << r << " out of range at " << col << "x" << row << std::endl;
			if (g >= 65536 || g < 0) std::cerr << "GValue " << g << " out of range at " << col << "x" << row << std::endl;
			if (b >= 65536 || b < 0) std::cerr << "BValue " << b << " out of range at " << col << "x" << row << std::endl;
			// Apply gamma correction
			*scanLine++ = qRgb(gamma[r], gamma[g], gamma[b]);
			//*scanLine++ = qRgb(r, g, b);
		}
	}
	qDebug() << "Render time " << t.elapsed() << " ms at " << QTime::currentTime().toString("hh:mm:ss.zzz").toUtf8().constData();
}


void RenderThread::run() {
	unsigned int _minx = 0, _miny = 0, _maxx = 0, _maxy = 0;
	forever {
		if (abort) return;

		QImage a(_maxx - _minx, _maxy - _miny, QImage::Format_RGB32);
		doRender(_minx, _miny, _maxx, _maxy, a);
		if (!restart && _maxy > 0) {
			emit renderedImage(_minx, _miny, images->getWidth(), images->getHeight(), a);
			yieldCurrentThread();
		}
		
		QImage b(images->getWidth(), images->getHeight(), QImage::Format_RGB32);
		doRender(0, 0, images->getWidth(), images->getHeight(), b);
         	mutex.lock();
		if (!restart) {
			emit renderedImage(0, 0, images->getWidth(), images->getHeight(), b);
			// Wait until render is called
			condition.wait(&mutex);
		}
		restart = false;
		_minx = minx;
		_miny = miny;
		_maxx = maxx;
		_maxy = maxy;
		images->setScale(scale);
		mutex.unlock();
	}
}

