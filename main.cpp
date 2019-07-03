#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>
#include <QTextCodec>

#include <iostream>

QTextStream conin(stdin);

enum ETerminalDirective
{
    E_TG_SET,
    E_TG_SET_LOCAL,
    E_TG_CLEAR,
    E_TG_GOTO,

    E_TG_TEXT,
    E_TG_TEXT_MULTILINE,
};

class CTerminalDirective
{
    public:
        ETerminalDirective eType;
        QString data;
};

class CTerminalOption
{
    public:
        QString transKey;
        QString transFallback;
        QString next;

        QStringList setMicrocommands;
};

class CTerminalSection
{
    public:
        QString expression;
        QList<CTerminalDirective*> directives;
        QList<CTerminalOption*> options;
};

class CTerminalInfoPortion
{
    public:
        QString key;
        int value;
};

class CTerminalDialog
{
    public:
        QList<CTerminalSection*> sections;
};

class CTerminalPlayer
{
    public:
        CTerminalDialog *pCurrentDialog;
        CTerminalSection *pCurrentSection;

        QList<QString> translations;
        QList<CTerminalInfoPortion*> infoPortions;

    public:

        int executeDirective(CTerminalDirective *pDirective)
        {
            switch (pDirective->eType)
            {
                case E_TG_TEXT_MULTILINE:
                case E_TG_TEXT: {
                    QString text;

                    // If text have translation key.
                    if (pDirective->data.startsWith("TTRS:")) {
                        pDirective->data.remove("TTRS:");

                        QStringList parts = pDirective->data.split("=");
                        //printf("%s", parts[1].toLatin1().data());
                        text = getTranslation(parts[0], parts[1]);//parts[1];
                    } else {
                        text = pDirective->data;
                    }

                    // Process special chars.
                    QStringList endParts = text.split("%");

                    printf("%s", endParts[0].toLocal8Bit().data());

                    if (endParts.count() > 1) {
                        for (int i = 1; i < endParts.count(); i++)
                        {
                            QString part = endParts[i];

                            if (part.startsWith("w")) {
                                part = part.right(part.length() - 1);

                                QThread::msleep(part.left(1).toUInt() * 100);
                                part = part.right(part.length() - 1);
                            }

                            printf("%s", part.toLocal8Bit().data());
                        }
                    }
                } break;

                case E_TG_GOTO: {
                    QListIterator<CTerminalSection*> it(pCurrentDialog->sections);
                    it.toFront();

                    while (it.hasNext())
                    {
                        CTerminalSection *pSection = it.next();

                        // TODO:
                        if (pSection->expression.contains(pDirective->data))
                        {
                            pCurrentSection = pSection;
                            return 1;
                        }
                    }
                } break;

                case E_TG_SET:
                case E_TG_SET_LOCAL: {
                    QListIterator<CTerminalInfoPortion*> it(infoPortions);

                    while (it.hasNext())
                    {
                        CTerminalInfoPortion *pInfoPortion = it.next();

                        // We aoready have that info portion.
                        if (pInfoPortion->key == pDirective->data) {
                            return 0;
                        }
                    }
                } break;

                case E_TG_CLEAR: {
                    QListIterator<CTerminalInfoPortion*> it(infoPortions);

                    while (it.hasNext())
                    {
                        CTerminalInfoPortion *pInfoPortion = it.next();

                        if (pInfoPortion->key == pDirective->data) {
                            infoPortions.removeOne(pInfoPortion);
                        }
                    }
                } break;
            }

            return 0;
        }

        CTerminalSection* processOption(CTerminalOption *pOption)
        {
            for (int i = 0; i < pOption->setMicrocommands.count(); i++)
            {
                // TODO:
            }

            QListIterator<CTerminalSection*> it(pCurrentDialog->sections);
            it.toFront();

            if (pOption->next == "MessageBoardInterface_On") {
                return 0;
            }

            while (it.hasNext())
            {
                CTerminalSection *pSection = it.next();

                // TODO:
                if (pSection->expression.contains(pOption->next))
                {
                    return pSection;
                }
            }

            return 0;
        }

        void execute(CTerminalDialog *pDialog)
        {
            pCurrentDialog = pDialog;

            pCurrentSection = pDialog->sections.first(); // Select first one.

            if (pDialog->sections.isEmpty()) {
                printf("Error! Missing dialog sections!\n");
            }

        process_section:
            //qInfo() << "Current: " << pCurrentSection->expression;

#ifdef PARSE_DBG
            printf("Section: %s\n", pCurrentSection->expression.toLatin1().data());
#endif

            QListIterator<CTerminalDirective*> it(pCurrentSection->directives);


            while (it.hasNext())
            {
                CTerminalDirective *pDirective = it.next();
                int result = executeDirective(pDirective);
                if (result == 1) {
                    goto process_section;
                }
            }

            for (int i = 0; i < pCurrentSection->options.count(); i++)
            {
                printf("%2d. %s\n", i + 1, getTranslation(pCurrentSection->options[i]->transKey, pCurrentSection->options[i]->transFallback).toLocal8Bit().data());
            }


            while (true)
            {
                QString input = conin.readLine();
                bool isInOk;

                unsigned int iSelected = input.toUInt(&isInOk, 10);

                if (!isInOk) {
                    continue;
                }

                if (iSelected == 0 || iSelected > pCurrentSection->options.count()) {
                    continue;
                }

                pCurrentSection = processOption(pCurrentSection->options[iSelected - 1]);

                if (pCurrentSection == 0) {
                    goto process_openeyes;
                }

                goto process_section;
            }

        process_openeyes:
            printf("\n");
        }

        QString getTranslation(QString key, QString fallback)
        {
            QListIterator<QString> it(translations);

            while (it.hasNext())
            {
                QString line = it.next();

                if (line.split("=")[0] == key)
                {
                    return line.split("=")[1];
                }
            }

            return fallback;
        }
};

CTerminalDialog* readDialog(QString filePath)
{
    CTerminalDialog *pDialog = new CTerminalDialog();

    printf("Reading file.\n");

    QFile file(filePath);

    bool isInTerminalBlock = false;
    bool isInOptionsBlock = false;
    bool isMultilineText = false;

    // If open successfully.
    if (file.open(QFile::ReadOnly))
    {
        QTextStream in(&file);
        int ctLines = 0;

        // While we have data.
        while (!in.atEnd())
        {
            QString line = in.readLine();
            ctLines++;

            // Skip comments.
            if (line.startsWith("#")) {
                continue;
            }

            if (isMultilineText) {
                if (line.contains("]]")) {
                    pDialog->sections.last()->directives.last()->data.append(line.split("]]")[0] + "\n");
                    isMultilineText = false;
                    continue;
                }

                pDialog->sections.last()->directives.last()->data.append(line + "\n");
                continue;
            }

            // Tf terminal section started.
            if (line.startsWith("terminal when (")) {

                if (isInTerminalBlock) {
                    printf("%d : Parse error! New terminal section before closing old one!\n", ctLines);
                    break;
                }


                QString rawCond = line.split("(")[1];
                QString readyCond = rawCond.split(")")[0]; // Extract condition string.

                isInTerminalBlock = true;

                CTerminalSection *pSection = new CTerminalSection();
                pSection->expression = readyCond;
                pDialog->sections.append(pSection);

#ifdef PARSE_DBG
                qInfo() << readyCond;
#endif

            } else if (line.startsWith("options:")) {
                isInOptionsBlock = true;
                continue; // Only next lines matter.
            }

            // Single-line Text.
            if (line.startsWith("text: \"")) {
                line = line.remove("text: \"");

                CTerminalDirective *pDirective = new CTerminalDirective;
                pDirective->eType = E_TG_TEXT;
                pDirective->data = line.split("\"")[0];
                pDialog->sections.last()->directives.append(pDirective);

#ifdef PARSE_DBG
                printf("  text\n");
#endif

            // Multi-line Text
            } else if (line.startsWith("text: [[")) {
                line = line.remove("text: [[");
                isMultilineText = true;

                if (line.contains("]]")) {
                    line = line.split("]]")[0];
                    isMultilineText = false;
                }

                CTerminalDirective *pDirective = new CTerminalDirective;
                pDirective->eType = E_TG_TEXT_MULTILINE;
                pDirective->data = line;
                pDialog->sections.last()->directives.append(pDirective);

#ifdef PARSE_DBG
                printf("  text multiline\n");
#endif

            // Goto
            } else if (line.startsWith("goto: ")) {
                line = line.remove("goto: ");

                CTerminalDirective *pDirective = new CTerminalDirective;
                pDirective->eType = E_TG_GOTO;
                pDirective->data = line;
                pDialog->sections.last()->directives.append(pDirective);

#ifdef PARSE_DBG
                printf("  goto\n");
#endif

            // Set Local
            } else if (line.startsWith("setlocal: ")) {
                line = line.remove("setlocal: ");

                CTerminalDirective *pDirective = new CTerminalDirective;
                pDirective->eType = E_TG_SET_LOCAL;
                pDirective->data = line;
                pDialog->sections.last()->directives.append(pDirective);

#ifdef PARSE_DBG
                printf("  setlocal\n");
#endif

            // Set
            } else if (line.startsWith("set: ")) {
                line = line.remove("set: ");

                CTerminalDirective *pDirective = new CTerminalDirective;
                pDirective->eType = E_TG_SET;
                pDirective->data = line;
                pDialog->sections.last()->directives.append(pDirective);

            // Clear
            } else if (line.startsWith("clear: ")) {
                line = line.remove("clear: ");

                CTerminalDirective *pDirective = new CTerminalDirective;
                pDirective->eType = E_TG_CLEAR;
                pDirective->data = line;
                pDialog->sections.last()->directives.append(pDirective);

#ifdef PARSE_DBG
                printf("  clear\n");
#endif
            }

            // Closing bracket
            if (line.contains("}")) {
                if (isInOptionsBlock) {
                    isInOptionsBlock = false;
                    continue;
                }

                if (isInTerminalBlock) {
                    isInTerminalBlock = false;
                    continue;
                }

                printf("%d: Parse error! Excess closing bracket!\n", ctLines);
                break;
            }

            if (isInOptionsBlock) {
                CTerminalOption *pOption = new CTerminalOption();

                QString option = line;

                QStringList parts = option.split('"');
                //qInfo() << parts.count();
                //qInfo() << parts[0];

                // Full trans name
                //qInfo() << parts[1];
                QStringList transParts = parts[1].split("=");
                pOption->transKey = transParts[0].remove("TTRS:");
                pOption->transFallback = transParts[1];

                // Parse additional commands.
                QString additionalCommands;

                // Skip short.
                if (parts.count() == 3) {
                    additionalCommands = parts[2].trimmed();
                // 5
                } else {
                    additionalCommands = parts[4].trimmed();
                }

                while (additionalCommands.length() > 0)
                {

                    if (additionalCommands.startsWith("next:")) {
                        additionalCommands = additionalCommands.right(additionalCommands.length() - 5).trimmed(); // Remove microcommand.


                        // Parse next.
                        QStringList parts2 = additionalCommands.split(" ");

                        additionalCommands = additionalCommands.remove(0, parts2[0].length());
                        pOption->next = parts2[0];

                        break;

                        // TODO: Check it.

                    } else if (additionalCommands.startsWith("set:")) {
                        additionalCommands = additionalCommands.right(additionalCommands.length() - 4).trimmed(); // Remove microcommand.

                        // Parse set.
                        QStringList parts2 = additionalCommands.split(" ");
                        additionalCommands = additionalCommands.remove(0, parts2[0].length());
                        pOption->setMicrocommands.append(parts2[0]);

                        // TODO: Check it.
                    }
                }

                pDialog->sections.last()->options.append(pOption);
            }

        }

        file.close();
    } else {
        printf("Failed to open file!\n");
    }

#ifdef PARSE_DBG
    printf("Done?\n");
#endif

    return pDialog;
}

QList<QString> readTranslations(QString path)
{
    QList<QString> result;
    QFile file(path);

    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);

        in.setCodec("UTF-8");

        while (!in.atEnd())
        {
            QString line = in.readLine();
            QString processedLine;

            QStringList parts = line.split("\\n");

            for (int i = 0; i < parts.count(); i++)
            {
                if (i == (parts.count() - 1)) {
                    processedLine += parts[i];
                } else {
                    processedLine += parts[i] + "\n";
                }
            }



            result.append(processedLine);
        }
    } else {
        printf("Unable to load translations: %s\n", path.toLatin1().data());
    }

    return result;
}

//#include <windows.h>

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, ""); // Enable russian locale.

    QCoreApplication a(argc, argv);

    //QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    CTerminalDialog *pDialog = readDialog("DLC_Sam.dlg");
    CTerminalPlayer *pPlayer = new CTerminalPlayer();
    pPlayer->translations = readTranslations("translation_DLC_01_Road_To_Gehenna.txt");

    //while (true)
    //{
    system("cls");
    pPlayer->execute(pDialog);

    a.exit(0);
    return 0;

    //}
    //qInfo() << pPlayer->getTranslation("TermDlg.Gehena.UrielPost.ConduitOfElohim", "<error>");
    //qDebug() << QString::fromUtf8("русский текст");

    //qInfo() << pPlayer->getTranslation("TermDlg.Gehena.UrielPost.ConduitOfElohim", "<error>");

    return a.exec();
}
