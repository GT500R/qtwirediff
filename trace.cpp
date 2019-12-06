#include "trace.h"
#include <QProcess>
#include <QXmlStreamReader>
#include <QBuffer>
#include <QDebug>

Trace::Trace(QObject *parent) : QObject(parent)
{

}

int Trace::loadTrace(const QString &fn)
{
    fn_ = fn;

    QProcess proc;
    QStringList args;

    args << "-r" << fn_;
    args << "-Y" << "!browser && (smb||smb2)";
    args << "-T" << "psml";

    proc.start("tshark", args, QProcess::ReadOnly);
    proc.waitForStarted();
    proc.waitForFinished();

    QByteArray out = proc.readAllStandardOutput();
    QBuffer outbuf;
    outbuf.setBuffer(&out);
    outbuf.open(QIODevice::ReadOnly);

    QXmlStreamReader xml;
    xml.setDevice(&outbuf);

    if (!xml.readNextStartElement())
        return -1;
    if (xml.name() != "psml")
        return -1;
    if (!xml.readNextStartElement())
        return -1;
    if (xml.name() != "structure")
        return -1;

    Q_ASSERT(xml.readNextStartElement() && xml.name() == "section" && xml.readNext() && xml.text() == "No."); xml.skipCurrentElement();
    Q_ASSERT(xml.readNextStartElement() && xml.name() == "section" && xml.readNext() && xml.text() == "Time"); xml.skipCurrentElement();
    Q_ASSERT(xml.readNextStartElement() && xml.name() == "section" && xml.readNext() && xml.text() == "Source"); xml.skipCurrentElement();
    Q_ASSERT(xml.readNextStartElement() && xml.name() == "section" && xml.readNext() && xml.text() == "Destination"); xml.skipCurrentElement();
    Q_ASSERT(xml.readNextStartElement() && xml.name() == "section" && xml.readNext() && xml.text() == "Protocol"); xml.skipCurrentElement();
    Q_ASSERT(xml.readNextStartElement() && xml.name() == "section" && xml.readNext() && xml.text() == "Length"); xml.skipCurrentElement();
    Q_ASSERT(xml.readNextStartElement() && xml.name() == "section" && xml.readNext() && xml.text() == "Info"); xml.skipCurrentElement();
    // end of <structure>
    xml.skipCurrentElement();

    if (!xml.readNextStartElement()) {
        // empty list
        return 0;
    }
    int i = 0;
    do {
        if (xml.name() != "packet")
            return -1;
        pkts_.append(Summary());

        if (!xml.readNextStartElement() || xml.name() != "section")
            return -1;
        xml.readNext();
        pkts_[i].no = xml.text().toInt();
        xml.skipCurrentElement();

        if (!xml.readNextStartElement() || xml.name() != "section")
            return -1;
        xml.readNext();
        pkts_[i].time = xml.text().toDouble();
        xml.skipCurrentElement();

        if (!xml.readNextStartElement() || xml.name() != "section")
            return -1;
        xml.readNext();
        pkts_[i].src = xml.text().toString();
        xml.skipCurrentElement();

        if (!xml.readNextStartElement() || xml.name() != "section")
            return -1;
        xml.readNext();
        pkts_[i].dst = xml.text().toString();
        xml.skipCurrentElement();

        if (!xml.readNextStartElement() || xml.name() != "section")
            return -1;
        xml.readNext();
        pkts_[i].proto = xml.text().toString();
        xml.skipCurrentElement();

        if (!xml.readNextStartElement() || xml.name() != "section")
            return -1;
        xml.readNext();
        // we don't care about length
        //pkts_[i].src = xml.text();
        xml.skipCurrentElement();

        if (!xml.readNextStartElement() || xml.name() != "section")
            return -1;
        xml.readNext();
        pkts_[i].info = xml.text().toString();
        xml.skipCurrentElement();

        // end of <packet>
        xml.skipCurrentElement();
        i++;

    } while (xml.readNextStartElement());

    loaded_ = true;
    return 0;
}

void Trace::dump()
{
    for (int i = 0; i < pkts_.size(); i++) {
        qDebug() << pkts_[i].no << " " << pkts_[i].time << " " << pkts_[i].src << " " << pkts_[i].dst << " " << pkts_[i].proto << " " << pkts_[i].info;
    }
}

QByteArray* Trace::getPDML(int no)
{
    if (!cache_.contains(no)) {
        QProcess proc;
        QStringList args;

        args << "-r" << fn_;
        args << "-Y" << (QString("frame.number == %1").arg(no));
        args << "-T" << "pdml";

        proc.start("tshark", args, QProcess::ReadOnly);
        proc.waitForStarted();
        proc.waitForFinished();

        QByteArray *out = new QByteArray(proc.readAllStandardOutput());
        cache_.insert(no, out);
    }
    return cache_[no];
}

void Trace::Node::dump(int n)
{
    {
        auto d = qDebug().nospace();
        for (int i = 0; i < n; i++)
            d << "    ";
        d << "name=" << name << " val=" << val;
    }
    for (int i = 0; i < children.size(); i++) {
        children[i]->dump(n+1);
    }
}

static Trace::Node* parseNode(QXmlStreamReader& xml)
{
    Trace::Node *n = new Trace::Node;

    if (!xml.isStartElement()) {
        qDebug("not at start element");
        throw Trace::ParseError();
    }

    if (xml.name() == "packet") {
        n->name = "root";
        n->val = "";
    } else {
        if (xml.name() != "proto" && xml.name() != "field") {
            qDebug("not at <proto> or <field>");
            throw Trace::ParseError();
        }

        n->name = xml.attributes().value("name").toString();
        n->val = (xml.attributes().hasAttribute("showname") ? xml.attributes().value("showname") : xml.attributes().value("show")).toString();
    }

    while (xml.readNextStartElement()) {
        n->children.append(parseNode(xml));
    }

    return n;
}

Trace::Node* Trace::getPacket(int no)
{
    QBuffer outbuf;
    outbuf.setBuffer(getPDML(no));
    outbuf.open(QIODevice::ReadOnly);

    QXmlStreamReader xml;
    xml.setDevice(&outbuf);

    if (!xml.readNextStartElement())
        throw ParseError();
    if (xml.name() != "pdml")
        throw ParseError();
    if (!xml.readNextStartElement())
        throw ParseError();
    if (xml.name() != "packet")
        throw ParseError();

    return parseNode(xml);
}
