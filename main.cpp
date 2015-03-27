#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/io.h>
#include <errno.h>
#include <math.h>
#include <limits>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <cstring>
#include <bitset>

#include "pci_codes.h"

#define CONFIG_ADDRESS	0x0CF8
#define CONFIG_DATA	    0x0CFC



typedef unsigned long	ulong;

using namespace std;

constexpr size_t _2x(size_t byteNum)
{
 //максимальное кол-во элементов,судя по колву-битов
  return pow(2,byteNum);
}

template < typename type>
struct Mask
{   //выбор  значения из середины числа типа type (байта,слова)
    type _mask;

    Mask(){  _mask = 0; }
    type reset(){ return _mask = 0;}

    template < typename valType>
    type value(type val,size_t part_no){    // выборка значения типа valType из числа type
        _mask = numeric_limits<valType>::max();
        size_t shLen = part_no * sizeof(valType) * 8;
        _mask <<= shLen;
        //маппинг
        val &= _mask;
        //сдвиг получ знач вправо
        return val >>= shLen;
    }

    type byte(type val,size_t b_no){    //выборка байта из числа val, по номеру b_no
        return value<unsigned char>(val,b_no);
    }
    type word(type val,size_t w_no){    //выборка слова
        return value<unsigned short>(val,w_no);
    }
};
Mask<ulong> _mask;

//максимальное кол-во шин,устр,функций...
	constexpr size_t _nBus  = _2x(8);
	constexpr size_t _nDev  = _2x(5);
	constexpr size_t _nFunc = _2x(3);
	constexpr size_t _nReg  = _2x(6);


template <typename type>
void _bin(type val) //вывод числа в двоичном виде
{
  //длина в битах
  size_t len = sizeof(type) * 8;
  //вывод в бинарном виде
  type mask = 1 << (len - 1);

  for(size_t i = 1; i <= len;i++)
  {
   putchar(val & mask ? '1' : '0');

   val <<= 1;

   if(i % 8 == 0) putchar(' ');
  }
}


ulong toConfAddrReg(ulong bus,ulong device,ulong func,ulong reg_index)
{
  //dw, сформированное для записи в регистр конфигурации адреса
  //   0-1  	резерв
  //   2-7  	знач рег для доступа к устр
  //   8-10	номер функции для многофункц устр
  //   11-15 	номер устр на шине PCI
  //   16-23 	номер шины
  //   24-30 	резерв
  //   31 	бит разрешения доступа к конф пространству PCI

  //сдвиг числовых значений на позиции опред форматом адреса для
  //вызова конфигурационного цикла
  bus <<= 16;
  device <<= 11;
  func <<= 8;
  reg_index <<= 2;
  // маппинг чисел на dw
  ulong addr = 0;
  addr |= bus;
  addr |= device;
  addr |= func;
  addr |= reg_index;
  //установка 31 бита в 1 (бит разрешения доступа к конфигурационному пространству
  //шины PCI)
  ulong mask = 1 << 31;
  addr |= mask;

  return addr;
}


PCI_DEVTABLE* devName(ulong devId,ulong venId)
{   //поиск по таблице имени устройства
    auto p = std::begin(PciDevTable);

	p = std::find_if(std::begin(PciDevTable),std::end(PciDevTable),[&devId,&venId](PCI_DEVTABLE& el){
					return el.DevId == devId && el.VenId == venId; });

	if(p == std::end(PciDevTable))
		throw string("unspecified device");

    return p;
}
PCI_VENTABLE* vendorName(ulong venId)
{   //поиск по таблице имени производителя
	auto it =  std::begin(PciVenTable);
	it = std::find_if(std::begin(PciVenTable),std::end(PciVenTable),[&venId](PCI_VENTABLE &el){
						return el.VenId == venId ;});
	if(it == std::end(PciVenTable))
		throw string("unspecified vendor");

    return it;
}

PCI_CLASSCODETABLE* devClass(ulong baseClass,ulong subClass, ulong progInterface){
    //поиск по табл класса устройства
    auto it = std::begin(PciClassCodeTable);
    it = std::find_if(std::begin(PciClassCodeTable),std::end(PciClassCodeTable),
                    [&baseClass,&subClass,&progInterface](PCI_CLASSCODETABLE &el){
                    return ( el.BaseClass == baseClass && el.SubClass == subClass && el.ProgIf == progInterface);
                    ;});
    if(it == std::end(PciClassCodeTable))
    {
        //попробовать поиск без учёта программного интерфейса
        it = std::find_if(std::begin(PciClassCodeTable),std::end(PciClassCodeTable),
                    [&baseClass,&subClass](PCI_CLASSCODETABLE &el){
                    return ( el.BaseClass == baseClass && el.SubClass == subClass);
                    ;});
    }
    if(it == std::end(PciClassCodeTable))
        throw string("unspecified device class");
    return it;
}
void showDevConfBlock(ulong bus,ulong dev,ulong func)
{
    //вывод заголовка конфигурационного блока устройства
//    VendorId
//    DeviceId
//    RevisionId
//    ClassId
//    SubsystemVendorId

    //чтение 0-ого рег (deviceId,vendorId)
    outl_p(toConfAddrReg(bus,dev,func,0),CONFIG_ADDRESS);
    ulong reg_0 = inl(CONFIG_DATA);

    ulong vendorId = _mask.word(reg_0,0);
    //проверка существования устройства
    if(vendorId == numeric_limits<unsigned short>::max() ||
       vendorId == 0)
        return;

    ulong deviceId = _mask.word(reg_0,1);

    //чтение RevisionId, ClassId
    outl_p(toConfAddrReg(bus,dev,func,2),CONFIG_ADDRESS);
    ulong reg_2 = inl_p(CONFIG_DATA);


    ulong revisionId = _mask.byte(reg_2,0);

    ulong baseClassCode = _mask.byte(reg_2,3);
    ulong subClassCode = _mask.byte(reg_2,2);
    ulong progInterface = _mask.byte(reg_2,1);

    //чтение subsystemVendorId
    outl_p(toConfAddrReg(bus,dev,func,6),CONFIG_ADDRESS);
    ulong reg_6 = inl_p(CONFIG_DATA);

    ulong subVenId = _mask.word(reg_6,0);


    //hex вывод
    cout.unsetf(ios::dec);
    cout.setf(ios::hex | ios::uppercase);

    cout<<endl;
    //вывод адреса устройства (номер шины:устройства:функции)
    cout<<"device address: bus "<<bus<<" device "<<dev<<" function "<<func<<endl;

    //16-разрядный код производителя (hex)
    cout<<"vendor id: "<<vendorId<<endl;
    //16-разрядный код суб-производителя
    cout<<"subsystem vendor id: "<<subVenId<<endl;
    //16-разрядный код устройства
    cout<<"device id: "<<deviceId<<endl;
    //8-разрядный код ревизии устройства
    cout<<"revision id: "<<revisionId<<endl;
    //3 8-разрядных поля class code
    cout<<"class code: "<<baseClassCode<<" "<<subClassCode<<" "<<progInterface<<endl;

    try{
    //производитель устройства
    auto venRow = vendorName(vendorId);
    cout<<"vendor name: "<<venRow->VenFull<<"  ("<<venRow->VenShort<<")"<<endl;
    }catch(string& e){cout<<e<<endl;}

    try{
    //название устройства
    auto devRow = devName(deviceId,vendorId);
    cout<<"device name: "<<devRow->Chip<<" "<<devRow->ChipDesc<<endl;
    }catch(string& e){cout<<e<<endl;}

    try{
    //класс устройства
    auto classRow = devClass(baseClassCode,subClassCode,progInterface);
    cout<<"device class: "<<classRow->BaseDesc<<"  "<<classRow->SubDesc<<"  "<<classRow->ProgDesc<<endl;
    }catch(string& e){cout<<e<<endl;}


    cout.unsetf(ios::hex | ios::uppercase);
    cout.setf(ios::dec);
    cout<<endl;
}

int main()
{
    //разрешение доступа ко всем портам
    if(iopl(3) == -1 )
        perror("ioperm");
    //cout<<_nBus<<" "<<_nDev<<" "<<_nFunc<<" "<<_nReg<<endl;
    //_bin(toConfAddrReg(0,3,0,0));

    //проходим по всем возможным устроствам
    for(size_t i = 0;i < _nBus;i++)
        for(size_t j = 0; j < _nDev;j++)
           for(size_t k = 0;k < _nFunc;k++)
                showDevConfBlock(i,j,k);

    cout<<endl;
	return 0;
}
