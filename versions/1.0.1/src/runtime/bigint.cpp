#include "astra/bigint.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace astra { namespace {
std::string norm(std::string s){auto p=s.find_first_not_of('0');return p==std::string::npos?"0":s.substr(p);}
int cmpAbs(const std::string&a,const std::string&b){if(a.size()!=b.size())return a.size()<b.size()?-1:1;if(a==b)return 0;return a<b?-1:1;}
std::string addAbs(const std::string&a,const std::string&b){int i=(int)a.size()-1,j=(int)b.size()-1,carry=0;std::string o;while(i>=0||j>=0||carry){int x=carry+(i>=0?a[i--]-'0':0)+(j>=0?b[j--]-'0':0);o.push_back(char('0'+x%10));carry=x/10;}std::reverse(o.begin(),o.end());return norm(o);}
std::string subAbs(const std::string&a,const std::string&b){int i=(int)a.size()-1,j=(int)b.size()-1,borrow=0;std::string o;while(i>=0){int x=(a[i--]-'0')-borrow-(j>=0?b[j--]-'0':0);borrow=x<0;if(borrow)x+=10;o.push_back(char('0'+x));}std::reverse(o.begin(),o.end());return norm(o);}
std::string mulAbs(const std::string&a,const std::string&b){std::vector<int>v(a.size()+b.size(),0);for(int i=(int)a.size()-1;i>=0;--i)for(int j=(int)b.size()-1;j>=0;--j)v[i+j+1]+=(a[i]-'0')*(b[j]-'0');for(int i=(int)v.size()-1;i>0;--i){v[i-1]+=v[i]/10;v[i]%=10;}std::string o;for(int x:v)o.push_back(char('0'+x));return norm(o);}
std::pair<std::string,std::string> divAbs(const std::string&a,const std::string&b){if(b=="0")throw std::runtime_error("BigInt division by zero");std::string q,r="0";for(char c:a){r=norm(r+c);int x=0;while(cmpAbs(r,b)>=0){r=subAbs(r,b);++x;}q.push_back(char('0'+x));}return {norm(q),norm(r)};}
BigInt make(bool n,std::string d){BigInt x;x.digits=norm(std::move(d));x.negative=n&&x.digits!="0";return x;}
}
BigInt BigInt::parse(const std::string&t){if(t.empty())throw std::runtime_error("Empty BigInt literal");size_t i=0;bool neg=false;if(t[i]=='+'||t[i]=='-'){neg=t[i]=='-';++i;}if(i==t.size())throw std::runtime_error("Invalid BigInt literal");std::string d;for(;i<t.size();++i){if(t[i]=='_')continue;if(t[i]<'0'||t[i]>'9')throw std::runtime_error("Invalid BigInt literal `"+t+"`");d+=t[i];}return make(neg,d);}
std::string BigInt::toString()const{return (negative?"-":"")+digits;}
int BigInt::compare(const BigInt&a,const BigInt&b){if(a.negative!=b.negative)return a.negative?-1:1;int c=cmpAbs(a.digits,b.digits);return a.negative?-c:c;}
BigInt BigInt::add(const BigInt&a,const BigInt&b){if(a.negative==b.negative)return make(a.negative,addAbs(a.digits,b.digits));int c=cmpAbs(a.digits,b.digits);if(c==0)return make(false,"0");return c>0?make(a.negative,subAbs(a.digits,b.digits)):make(b.negative,subAbs(b.digits,a.digits));}
BigInt BigInt::subtract(const BigInt&a,const BigInt&b){BigInt x=b;x.negative=!x.negative&&x.digits!="0";if(b.negative)x.negative=false;return add(a,x);}
BigInt BigInt::multiply(const BigInt&a,const BigInt&b){return make(a.negative!=b.negative,mulAbs(a.digits,b.digits));}
BigInt BigInt::divide(const BigInt&a,const BigInt&b){auto q=divAbs(a.digits,b.digits).first;return make(a.negative!=b.negative,q);}
BigInt BigInt::remainder(const BigInt&a,const BigInt&b){auto r=divAbs(a.digits,b.digits).second;return make(a.negative,r);}
}
