// abstractbytearraymodel.sip generated by MetaSIP on Mon Oct  8 00:06:00 2012
//
// This file is part of the okteta Python extension module.
//
// Copyright (C) 2012 Shaheed Haque <srhaque@theiet.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) version 3.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with this library; see the file COPYING.LIB.  If not, write to
// the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
// Boston, MA 02110-1301, USA.


namespace Okteta
{
%TypeHeaderCode
#include <abstractbytearraymodel.h>
%End

    class AbstractByteArrayModel : QObject /Abstract/
    {
%TypeHeaderCode
#include <abstractbytearraymodel.h>
%End

    protected:
        explicit AbstractByteArrayModel(QObject *parent = 0);

    public:
        virtual ~AbstractByteArrayModel();
        virtual Okteta::Byte byte(Okteta::Address offset) const = 0;
        virtual Okteta::Size size() const = 0;
        virtual bool isReadOnly() const;
        virtual bool isModified() const = 0;
        virtual Okteta::Size insert(Okteta::Address offset, const Okteta::Byte *insertData, int insertLength);
        virtual Okteta::Size remove(const Okteta::AddressRange &removeRange);
        virtual Okteta::Size replace(const Okteta::AddressRange &removeRange, const Okteta::Byte *insertData, int insertLength) = 0;
        virtual bool swap(Okteta::Address firstStart, const Okteta::AddressRange &secondRange) = 0;
        virtual Okteta::Size fill( Okteta::Byte fillByte, Okteta::Address offset = 0, Okteta::Size fillLength = -1 ) = 0; // Manually added via MetaSIP;
        virtual void setByte(Okteta::Address offset, Okteta::Byte byte) = 0;
        virtual void setModified(bool modified) = 0;
        virtual void setReadOnly(bool isReadOnly);
        virtual Okteta::Address indexOf(const Okteta::Byte *pattern, int patternLength, Okteta::Address fromOffset = 0, Okteta::Address toOffset = -1) const; // Manually added via MetaSIP;
        Okteta::Address indexOfCaseInsensitive(const Okteta::CharCodec *charCodec, const QByteArray &pattern, Okteta::Address fromOffset = 0, Okteta::Address toOffset = -1) const; // Manually added via MetaSIP;
        virtual Okteta::Address lastIndexOf(const Okteta::Byte *pattern, int patternLength, Okteta::Address fromOffset = -1, Okteta::Address toOffset = 0) const; // Manually added via MetaSIP;
        Okteta::Address lastIndexOfCaseInsensitive(const Okteta::CharCodec *charCodec, const QByteArray &pattern, Okteta::Address fromOffset = -1, Okteta::Address toOffset = 0) const; // Manually added via MetaSIP;

    protected:
        void readOnlyChanged(bool isReadOnly);
        void modifiedChanged(bool isModified);
        void searchedBytes(Okteta::Size bytes) const;

    public:
        Okteta::Size insert(Okteta::Address offset, const QByteArray &insertData);
        Okteta::Size remove(Okteta::Address offset, Okteta::Size removeLength);
        Okteta::Size replace(Okteta::Address offset, Okteta::Size removeLength, const Okteta::Byte *insertData, Okteta::Size insertLength);
        Okteta::Size copyTo(Okteta::Byte *dest, Okteta::Address offset, Okteta::Size copyLength) const;
        Okteta::Address indexOf(const QByteArray &pattern, Okteta::Address fromOffset = 0, Okteta::Address toOffset = -1) const; // Manually added via MetaSIP;
        Okteta::Address lastIndexOf(const QByteArray &pattern, Okteta::Address fromOffset = -1, Okteta::Address toOffset = 0) const; // Manually added via MetaSIP;
    };
};
