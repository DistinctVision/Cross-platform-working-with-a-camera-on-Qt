#ifndef CAMERASURFACEWIDGET_H
#define CAMERASURFACEWIDGET_H

#include <QOpenGLWidget>
#include <QVideoFrame>
#include <QAbstractVideoSurface>
#include <QAbstractVideoBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>

//Класс для получения кадра с камеры
class CameraSurface: public QAbstractVideoSurface
{
    Q_OBJECT
public:
    explicit CameraSurface(QObject* parent = 0);

    bool present(const QVideoFrame& frame) Q_DECL_OVERRIDE;//в этот метод посылается кадр
    //поддерживаемые форматы
    QList<QVideoFrame::PixelFormat> supportedPixelFormats(
            QAbstractVideoBuffer::HandleType type = QAbstractVideoBuffer::NoHandle) const Q_DECL_OVERRIDE;
    //а этот вызывается при начале работы
    bool start(const QVideoSurfaceFormat& format) Q_DECL_OVERRIDE;
    bool isFrameAvailable() const { return _frameAvailable; }// а есть ли новый кадр
    QVideoFrame& frame()//получить текущий кадр
    {
        _frameAvailable = false;//получили, а значит нового уже нет
        return _frame;
    }
    //устанавливаем OpenGLContext, который надо будет отправить
    void setOpenGLContext(QOpenGLContext* glContext) { _glContext = glContext; }
    //вызываем его отправление
    void scheduleOpenGLContextUpdate();

private slots:
    void updateOpenGLContext();//слот, чтобы вызвать обновление в нужном потоке

private:
    QOpenGLContext* _glContext;
    QVideoFrame _frame;
    bool _frameAvailable;
};

//виджет для отображения кадра
class CameraSurfaceWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit CameraSurfaceWidget(QWidget* parent = 0);
    ~CameraSurfaceWidget();

    CameraSurface* surface() { return _surface; }

private:
    CameraSurface* _surface;//с помощью этого объекта, мы получаем кадры с камеры
    QOpenGLContext* _glContext;//контекст OpenGL
    QOpenGLFunctions* _glFuncs;//класс его функций
    //А это информация, для отрисоки квадрата с кадром камеры
    QOpenGLBuffer _indicies;
    QOpenGLBuffer _vertices;
    QOpenGLBuffer _textureCoords;
    //Текструа кадра. Будет создана, если данные о кадре будут приходить в ввиде массива, а не текстуры
    GLuint _frameTexture;
    //параметры кадра
    QSize _frameSize;
    QVideoFrame::PixelFormat _framePixelFormat;
    //Шейдер, который рисует прямоугольник
    QOpenGLShaderProgram* _shaderProgram;

    void _initializeShaderProgram(QVideoFrame::PixelFormat pixelFormat);
    void _initializeShaderProgram(const QString& rgb = "rgb");
    void initializeGL() Q_DECL_OVERRIDE;//вызввается при инициализации
    void paintGL() Q_DECL_OVERRIDE;//отрисовке
};

#endif // QCAMERASURFACEWIDGET_H
