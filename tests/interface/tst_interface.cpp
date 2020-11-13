#include <QtTest>
#include <QCoreApplication>

#include <qosc.h>
#include <QNetworkDatagram>

class interface : public QObject
{
    Q_OBJECT

public:
    interface();
    ~interface();

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_send();
    void test_receive();
    void test_connect_dispatch();
    void test_time_bundle();

    // These guyes are just here tot est if the examples in the README actually
    // build
    void test_readme_server();
    void test_readme_client();

public:
    void verify_send(QByteArray comp);
    void send(QByteArray comp);
    QByteArray msgSocketError(const QAbstractSocket &s);
    QOSCMessage::ptr magicMessage();
    QOSCBundle::ptr magicBundle();

public:
    const QHostAddress localAddr{"127.0.0.1"};
    const quint16      localPort = 8000;
    const QHostAddress remoteAddr{"127.0.0.1"};
    const quint16      remotePort = 9000;

    QOSCInterface test;
    QUdpSocket    echo;
};

interface::interface() {}

interface::~interface() {}

void interface::initTestCase()
{
    QSignalSpy spyLocalPort(&test, SIGNAL(localPortChanged(quint16)));
    QSignalSpy spyLocalAddr(&test, SIGNAL(localAddrChanged(QHostAddress)));
    QSignalSpy spyRemotePort(&test, SIGNAL(remotePortChanged(quint16)));
    QSignalSpy spyRemoteAddr(&test, SIGNAL(remoteAddrChanged(QHostAddress)));

    test.setLocalPort(localPort);
    test.setRemoteAddr(remoteAddr);
    test.setRemotePort(remotePort);

    QVERIFY(QTest::qWaitFor([=](){return test.isListening();}));

    QCOMPARE(spyLocalPort.count(), 1);
    QCOMPARE(spyLocalAddr.count(), 0);
    QCOMPARE(spyRemotePort.count(), 1);
    QCOMPARE(spyRemoteAddr.count(), 0);

    QCOMPARE(spyLocalPort[0][0].toInt(), localPort);
    QCOMPARE(spyRemotePort[0][0].toInt(), remotePort);

    QCOMPARE(test.localAddr(), localAddr);
    QCOMPARE(test.localPort(), localPort);
    QCOMPARE(test.remoteAddr(), remoteAddr);
    QCOMPARE(test.remotePort(), remotePort);

    echo.bind(remoteAddr, remotePort);
    QVERIFY(QTest::qWaitFor([=](){return echo.isValid();}));
}

void interface::cleanupTestCase() {}

void interface::test_send()
{
    test.send("/a/b/c", 10);
    verify_send(magicMessage()->package());

    auto value = QOSC::makeValue(10);
    test.send("/a/b/c", value);
    verify_send(magicMessage()->package());

    QOSCMessage msg("/a/b/c", value);
    test.send(msg);
    verify_send(magicMessage()->package());

    QOSCBundle b;
    b.time = QOSCTimeTag::asap();
    b.elements << QOSCMessage::ptr::create("/a/b/c", value);
    test.send(b);
    verify_send(QByteArray("#bundle" // bundle mark
                           "\x00\x00\x00\x00\x00\x00\x00\x01" // timestamp
                           "\x00\x00\x00\x10" // first element size
                           "/a/b/c\x00\x00,i\x00\x00\x00\x00\x00\x0A" // first element content
                           "\x00" // padding
                           , 36));
}

void interface::test_receive()
{
    QSignalSpy spy(&test, SIGNAL(packetReceived(QOSCPacket::ptr)));

    {
        send(magicMessage()->package());
        QVERIFY(spy.wait());
        auto packet = spy.last().first().value<QOSCPacket::ptr>();
        QCOMPARE(packet->type, QOSCPacket::OSCMessage);
        auto msg = packet.dynamicCast<QOSCMessage>();
        QCOMPARE(msg->pattern, "/a/b/c");
        QCOMPARE(msg->args[0]->toInt(), 10);
    }

    {
        send(magicBundle()->package());
        QVERIFY(spy.wait());
        auto packet = spy.last().first().value<QOSCPacket::ptr>();

        QCOMPARE(packet->type, QOSCPacket::OSCBundle);

        auto b3 = packet.dynamicCast<QOSCBundle>();

        QCOMPARE(b3->time.toUint64(), 1ul);

        QCOMPARE(b3->size(), 2);

        QCOMPARE(b3->first()->type, QOSCPacket::OSCMessage);

        auto msg2 = b3->first().dynamicCast<QOSCMessage>();

        QCOMPARE(msg2->pattern, "/a/b/c");
        QCOMPARE(msg2->valueType(), QOSC::Int32Type);
        QCOMPARE(msg2->toInt(), 10);

        QCOMPARE(b3->last()->type, QOSCPacket::OSCBundle);

        auto b4 = b3->last().dynamicCast<QOSCBundle>();

        QCOMPARE(b4->time.toUint64(), 1ull);

        QCOMPARE(b4->size(), 1);

        QCOMPARE(b4->first()->type, QOSCPacket::OSCMessage);

        auto msg3 = b4->first().dynamicCast<QOSCMessage>();

        QCOMPARE(msg3->pattern, "/a/b/c");
        QCOMPARE(msg3->valueType(), QOSC::Int32Type);
        QCOMPARE(msg3->toInt(), 10);
    }
}

void interface::test_connect_dispatch()
{
    bool abc = false;
    bool adc = false;

    test.connect("/a/b/c",
    [&](const QOSCMessage::ptr& m)
    {
        QCOMPARE(m->pattern, "/a/?/c");
        QCOMPARE(m->args[0]->toInt(), 10);
        abc = true;
    });

    test.connect("/a/d/c",
    [&](const QOSCMessage::ptr& m)
    {
        QCOMPARE(m->pattern, "/a/?/c");
        QCOMPARE(m->args[0]->toInt(), 10);
        adc = true;
    });

    auto msg = magicMessage();
    msg->pattern = "/a/?/c";
    send(msg->package());

    QVERIFY(QTest::qWaitFor([&](){ return abc && adc; }));

    test.disconnect();
}

void interface::test_time_bundle()
{
    int abc = 0;

    test.connect("/a/b/c",
    [&](const QOSCMessage::ptr& m)
    {
        QCOMPARE(m->pattern, "/a/b/c");
        QCOMPARE(m->args[0]->toInt(), 10);
        abc++;
    });

    send(magicBundle()->package());

    QVERIFY(QTest::qWaitFor([&](){ return abc == 2; }, 100));

    auto bundle = magicBundle();
    bundle->time = QDateTime::currentDateTime().addSecs(10);
    bundle->elements[1].dynamicCast<QOSCBundle>()->time.fromUint64(0xFFFFFFFFFFFFFFFFull);

    QElapsedTimer t;
    t.start();
    send(bundle->package());

    QVERIFY(QTest::qWaitFor([&](){ return abc > 2; }, 11000));
    qDebug() << t.elapsed();
}

void interface::test_readme_server()
{
    // Bind the network interface so you can send and get messages
    QOSCInterface iface;
    iface.setRemoteAddr(QHostAddress("192.168.0.10"));
    iface.setRemotePort(9000);
    iface.setLocalPort(8000);

    // Connect callbacks to get notified of new messages
    iface.connect("/my/osc/pattern",
    [](const QOSCMessage::ptr& msg)
    {
        // This is the message callback
        // It'll be called each time a message matching the
        // pattern you set is received.

        for(auto& arg : *msg)
        {
            if(arg->type != QOSC::Int32Type)
                continue;

            int i = arg->toInt();

            // do stuff with i

            Q_UNUSED(i);
        }
    });

    // Alternatively you can use any QObject slots
    QObject obj;
    iface.connect("/my/other/pattern", &obj, SLOT(mySlot(const QOSCMessage::ptr& msg)));

    // Or get all messages and dispatch them yourself
    QObject dispatcher;
    QObject::connect(&iface,      SIGNAL(packetReceived(const QOSCPacket::ptr& ptr)),
                     &dispatcher, SLOT(dispatch(const QOSCPacket::ptr& ptr)));
}

void interface::test_readme_client()
{
    // Bind the network interface so you can send and get messages
    QOSCInterface iface;
    iface.setRemoteAddr(QHostAddress("192.168.0.10"));
    iface.setRemotePort(9000);
    iface.setLocalPort(8000);

    // Craft the message you want to send
    auto msg = QOSCMessage::ptr::create("/my/osc/pattern", QString("Some random string"));

    iface.send(msg);

    // More complex messages with several values
    auto myInt = QOSC::makeValue(10);
    auto myMidi = QOSC::makeValue(/*port:   0       */ 0x00,
                                  /*status: note on */ 0x90,
                                  /*data1:  note A4 */ 0x45,
                                  /*data2:  note vel*/ 0x10);
    auto myColor = QOSC::makeValue(QColor(Qt::green));

    auto msg2 = QOSCMessage::ptr::create("/my/osc/pattern");
    *msg2 << myInt << myMidi << myColor;

    iface.send(msg2);

    // You can also create bundles
    // They are useful to deliver several messages to different patterns
    // They can also be timed, You can specify an absolute Date and Time when you want them to be executed

    auto bundle = QOSCBundle::ptr::create();
    bundle->time = QDateTime::currentDateTime().addSecs(15); // The bundle is for 15sec later
    *bundle << QOSCMessage::ptr::create("/osc/pattern1", QString("Some random string"))
            << QOSCMessage::ptr::create("/osc/pattern2", 125)
            << QOSCMessage::ptr::create("/osc/pattern3", true)
            << QOSCMessage::ptr::create("/osc/pattern4", 3.14);

    iface.send(bundle);
}

void interface::verify_send(QByteArray comp)
{
    QVERIFY2(echo.waitForReadyRead(10000), msgSocketError(echo).constData());

    QVERIFY(echo.hasPendingDatagrams());

    auto datagram = echo.receiveDatagram();

    QVERIFY(!echo.hasPendingDatagrams());

    QCOMPARE(datagram.data(), comp);
}

void interface::send(QByteArray comp)
{
    echo.writeDatagram(comp, localAddr, localPort);
}

QByteArray interface::msgSocketError(const QAbstractSocket &s)
{
    QString result;
    QDebug debug(&result);
    debug.nospace();
    debug.noquote();
    if (!s.localAddress().isNull())
        debug << "local=" << s.localAddress().toString() << ':' << s.localPort();
    if (!s.peerAddress().isNull())
        debug << ", peer=" << s.peerAddress().toString() << ':' << s.peerPort();
    debug << ", type=" << s.socketType() << ", state=" << s.state()
        << ", error=" << s.error() << ": " << s.errorString();
   return result.toLocal8Bit();
}

QOSCMessage::ptr interface::magicMessage()
{
    return QOSCMessage::ptr::create("/a/b/c", 10);
}

QOSCBundle::ptr interface::magicBundle()
{
    QOSCMessage::ptr msg(new QOSCMessage("/a/b/c", 10));
    QOSCBundle::ptr b1(new QOSCBundle());
    QOSCBundle::ptr b2(new QOSCBundle());

    b1->time = QOSCTimeTag::asap();
    b1->elements << msg;

    b2->time = QOSCTimeTag::asap();
    b2->elements << msg;

    b1->elements << b2;
    return b1;
}

QTEST_MAIN(interface)

#include "tst_interface.moc"