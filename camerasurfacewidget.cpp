#include "camerasurfacewidget.h"
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QVideoSurfaceFormat>

void CameraSurface::scheduleOpenGLContextUpdate()
{
    //Обновление контекста нужно вызвать в потоке объекта
    QMetaObject::invokeMethod(this, "updateOpenGLContext");
}

void CameraSurface::updateOpenGLContext()
{
    if (_glContext == 0)
        return;
    //свойство "GLContext" уже создано, нужно только в него информацию отправить.
    this->setProperty("GLContext", QVariant::fromValue<QObject*>(_glContext));
}

CameraSurface::CameraSurface(QObject* parent): QAbstractVideoSurface(parent)
{
    _frameAvailable = false;
    _glContext = 0;
    _frame = QVideoFrame();
}

QList<QVideoFrame::PixelFormat> CameraSurface::supportedPixelFormats(
            QAbstractVideoBuffer::HandleType handleType) const
{
    if (handleType == QAbstractVideoBuffer::NoHandle) {//это значит, что данные будут в ввиде массива
        return QList<QVideoFrame::PixelFormat>()
                << QVideoFrame::Format_RGB32
                << QVideoFrame::Format_BGR32;
    } else if (handleType == QAbstractVideoBuffer::GLTextureHandle) {//текстуры
        return QList<QVideoFrame::PixelFormat>()
                << QVideoFrame::Format_RGB32
                << QVideoFrame::Format_BGR32;
    }
    return QList<QVideoFrame::PixelFormat>();//а в другом виде мы не принимаем
}

bool CameraSurface::start(const QVideoSurfaceFormat& format)
{
    //нужно проверить, можем ли мы принимать данные в таком виде
    if (!supportedPixelFormats(format.handleType()).contains(format.pixelFormat())) {
        qDebug() << format.handleType() << " " << format.pixelFormat() << " - format is not supported.";
        return false;
    }
    //можем!
    return QAbstractVideoSurface::start(format);
}

bool CameraSurface::present(const QVideoFrame& frame)
{
    if ((_frameAvailable) || (_glContext == 0))
        return true;//если кадр уже есть или конекст OpenGL еще не инициализирован, то смысла в сохранении нового кадра пока нет.
    if (!frame.isValid())//на всякий случай
        return false;
    _frame = frame;//копируем весь кадр.
    //! Важно! Нельзя считывать данные с кадра в этом потоке, так как в случае, если данные передаются в виде текстуры,
    //! это вызвывет создание объектов ее передачи. Произойдет попытка захвата текущего контекста OpenGL. А в потоке,
    //! в котором вызывается эта функция его попросту нет.
    _frameAvailable = true;
    QWidget* parentWidget = dynamic_cast<QWidget*>(parent());
    if (parentWidget)
        parentWidget->update();//вызываем обновление окна
    return true;
}

CameraSurfaceWidget::CameraSurfaceWidget(QWidget* parent) : QOpenGLWidget(parent)
{
    _surface = new CameraSurface(this);
    _shaderProgram = 0;
    _frameTexture = 0;
    _framePixelFormat = QVideoFrame::Format_Invalid;
    _frameSize = QSize(0, 0);
}

void CameraSurfaceWidget::_initializeShaderProgram(QVideoFrame::PixelFormat pixelFormat)
{
    //Странная вещь. Если формат - Format_RGB32, то каналы располагаются в поряжке "bgr".
    //Если формат - Format_BGR32, то "rgb". Что-то в Qt напутали.
    if (pixelFormat == QVideoFrame::Format_RGB32)
        _initializeShaderProgram("bgr");
    else // if (pixelFormat == QVideoFrame::Format_BGR32)
        _initializeShaderProgram("rgb");
}

void CameraSurfaceWidget::_initializeShaderProgram(const QString& rgb)
{
    if (_shaderProgram) {
        delete _shaderProgram;
    }
    _shaderProgram = new QOpenGLShaderProgram();
    _shaderProgram->create();
    //Я не знаю, почему текстурные координаты на андроиде нужно так менять.
    //Опять какие-то странности.
    const QString shaderVertexCode =
            "attribute highp vec2 vertex_position;\n"
            "attribute highp vec2 vertex_texcoord;\n"
            "varying highp vec2 v_texcoord;\n"
            "void main()\n"
            "{\n"
            "    gl_Position = vec4(vertex_position, 0.0, 1.0);\n"
#ifdef __ANDROID__
            "    v_texcoord = (vertex_texcoord + vec2(1.0, 1.0)) * 0.5;\n"
#else
            "    v_texcoord = vertex_texcoord;\n"
#endif
            "}";
    QOpenGLShader* shaderVertex = new QOpenGLShader(QOpenGLShader::Vertex);
    shaderVertex->compileSourceCode(shaderVertexCode);
    _shaderProgram->addShader(shaderVertex);
    const QString shaderFragmentCode =
            "uniform sampler2D texture;\n"
            "varying highp vec2 v_texcoord;\n"
            "void main()\n"
            "{\n"   //как мы помним порядок канналов цвета может меняться, в зависимости от формата.
            "    gl_FragColor = vec4(texture2D(texture, v_texcoord)." + rgb + ", 1.0);\n"
            "}";
    QOpenGLShader* shaderFragment = new QOpenGLShader(QOpenGLShader::Fragment);
    shaderFragment->compileSourceCode(shaderFragmentCode);
    _shaderProgram->addShader(shaderFragment);
    _shaderProgram->bindAttributeLocation("vertex_position", 0);
    _shaderProgram->bindAttributeLocation("vertex_texcoord", 1);
    _shaderProgram->link();
}

CameraSurfaceWidget::~CameraSurfaceWidget()
{
    delete _surface;
    if (_shaderProgram)
        delete _shaderProgram;
    _indicies.destroy();
    _vertices.destroy();
    _textureCoords.destroy();
    if (_frameTexture != 0)//если создали текстуру, нужно ее удалить
        _glFuncs->glDeleteTextures(1, &_frameTexture);
}

void CameraSurfaceWidget::initializeGL()
{
    _glContext = QOpenGLContext::currentContext();

    // Установим контекст OpenGL, чтобы он мог потом его передать
    _surface->setOpenGLContext(_glContext);
    // Получить объект функции и назначить все точки входа
    _glFuncs = _glContext->functions();
    _glFuncs->initializeOpenGLFunctions();
    _glFuncs->glEnable(GL_TEXTURE_2D);

    //треугольники
    _indicies.create();
    _indicies.bind();
    GLuint t[] = { 0, 1, 2, 0, 2, 3 };//здесь их два
    _indicies.allocate(t, 6 * sizeof(GLuint));

    //вершины
    _vertices.create();
    _vertices.bind();
    QVector2D v[] = { QVector2D(-1.0f, 1.0f), QVector2D(1.0f, 1.0f), QVector2D(1.0f, -1.0f), QVector2D(-1.0f, -1.0f) };
    _vertices.allocate(v, 4 * sizeof(QVector2D));

    //текстурные координаты
    _textureCoords.create();
    _textureCoords.bind();
    QVector2D tc[] = { QVector2D(0.0f, 1.0f), QVector2D(1.0f, 1.0f), QVector2D(1.0f, 0.0f), QVector2D(0.0f, 0.0f) };
    _textureCoords.allocate(tc, 4 * sizeof(QVector2D));
}

void CameraSurfaceWidget::paintGL()
{
    if (!_surface->isActive()) {//если мы не начали принимать кадры с камеры, то
        _surface->scheduleOpenGLContextUpdate();//нужно отправить данные о контексте OpenGL
        QObject* glThreadCallback = (_surface->property("_q_GLThreadCallback")).value<QObject*>();//куда отправляем событие, говорящее,
        //что все готово к принятию видеопотока?
        if (glThreadCallback) {
            QEvent event(QEvent::User);//Это по идее пользовательский флаг. Но эй, все равно никто не должен знать об этом костыле!
            glThreadCallback->event(&event);//теперь отправляем это событие
        }
        //И эта часть выше не нужна для винды. Но, главное, она там ничего не сломает.
    } else {
        QVideoFrame& frame = _surface->frame();
        GLuint texture = 0;//Здесь будет тектура кадры. А появиться она может по-разному.
        if (frame.handleType() == QAbstractVideoBuffer::NoHandle) {//Если присылают массив
            if (_frameTexture == 0) {//создаем свою текстуру, если еще не создали
                _glFuncs->glGenTextures(1, &_frameTexture);
                _frameSize = QSize(0, 0);
                _framePixelFormat = QVideoFrame::Format_Invalid;//в каком формает еще не знаем
            }
            _glFuncs->glBindTexture(GL_TEXTURE_2D, _frameTexture);
            if ((_frameSize != frame.size()) || (_framePixelFormat != frame.pixelFormat())) {//если что то не совпадает,
                _frameSize = frame.size();
                _framePixelFormat = frame.pixelFormat();
                _initializeShaderProgram(frame.pixelFormat());//то пересоздаем
                if (frame.map(QAbstractVideoBuffer::ReadOnly)) {//если нам разрешают читать массиа
                    //отправляем его в текстуру
                    _glFuncs->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _frameSize.width(), _frameSize.height(), 0,
                                       GL_RGBA, GL_UNSIGNED_BYTE, frame.bits());//GL_RGBA - работает везде, поэтому здесь именно он
                    frame.unmap();//не забываем закрыть за собой
                }
            } else {
                if (frame.map(QAbstractVideoBuffer::ReadOnly)) {
                    //обновляем данные
                    _glFuncs->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _frameSize.width(), _frameSize.height(),
                                          GL_RGBA, GL_UNSIGNED_BYTE, frame.bits());
                    frame.unmap();
                }
            }
            _glFuncs->glGenerateMipmap(GL_TEXTURE_2D);//если это не сделать, не будет видно изменений
            texture = _frameTexture;
        } else if (frame.handleType() == QAbstractVideoBuffer::GLTextureHandle) {//Если присылаеют текстуру
            if (_frameTexture) {//Если создали текстуру,
                _glFuncs->glDeleteTextures(1, &_frameTexture);//то она уже не нужна
                _frameTexture = 0;
            }
            if ((_frameSize != frame.size()) || (_framePixelFormat != frame.pixelFormat())) {//проверяем формат
                _frameSize = frame.size();
                _framePixelFormat = frame.pixelFormat();
                _initializeShaderProgram(frame.pixelFormat());
            }
            texture = frame.handle().toUInt();//! А вот здесь, помимо получения текстуры, происходит всякая магия осуществления ее передачи.
            //! Требуется наш контекст OpenGL и в этом потоке. Поэтому эту функцию надо вызывать только здесь.
        }
        if (texture != 0) {
            //это все необязательно
            /*_glFuncs->glViewport(0, 0, width(), height());
            _glFuncs->glDisable(GL_DEPTH_TEST);
            _glFuncs->glDepthMask(GL_FALSE);*/
            _shaderProgram->bind();//устанавливаем наш шейдер
            _shaderProgram->enableAttributeArray(0);//Мы хотим отправить вершины
            _shaderProgram->enableAttributeArray(1);//и тектурные координаты
            _glFuncs->glActiveTexture(GL_TEXTURE0);//и текстуру
            _glFuncs->glBindTexture(GL_TEXTURE_2D, texture);//вот эту
            _shaderProgram->setUniformValue("texture", 0);//"texture" - будет нулевой тектурой
            _glFuncs->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indicies.bufferId());//отправляем треугольники
            _glFuncs->glBindBuffer(GL_ARRAY_BUFFER, _vertices.bufferId());//вершины
            _glFuncs->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QVector2D), 0);//которые у нас с индексом о
            _glFuncs->glBindBuffer(GL_ARRAY_BUFFER, _textureCoords.bufferId());//текстурные координаты
            _glFuncs->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QVector2D), 0);//которые с индексом 1
            _glFuncs->glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);//и рисуем
        }
    }
    update();//если этого здесь не сделать, то на андроиде не будет обновления окна
}
