#pragma once
#include <string>
namespace astra {
struct BigInt {
    bool negative=false;
    std::string digits="0";
    static BigInt parse(const std::string& text);
    std::string toString() const;
    static int compare(const BigInt&,const BigInt&);
    static BigInt add(const BigInt&,const BigInt&);
    static BigInt subtract(const BigInt&,const BigInt&);
    static BigInt multiply(const BigInt&,const BigInt&);
    static BigInt divide(const BigInt&,const BigInt&);
    static BigInt remainder(const BigInt&,const BigInt&);
};
}
