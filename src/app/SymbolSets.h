#pragma once

#include <QString>
#include <QStringList>

#include <vector>

// The characters offered by the symbol panel, by category.
struct SymbolSet {
    QString name;
    QStringList symbols;
};

std::vector<SymbolSet> symbolSets();
