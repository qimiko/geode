#include "Shared.hpp"
#include "TypeOpt.hpp"

namespace { namespace format_strings {
	char const* source_start = R"CAC(
#include <stdexcept>
#include <Geode/Bindings.hpp>
#include <Geode/utils/addresser.hpp>
#include <Geode/modify/Addresses.hpp>
#include <Geode/modify/Traits.hpp>
#include <Geode/loader/Tulip.hpp>

using namespace geode;
using namespace geode::modifier;
using cocos2d::CCDestructor;

std::unordered_map<void*, bool>& CCDestructor::destructorLock() {{
	static auto ret = new std::unordered_map<void*, bool>;
	return *ret;
}}
bool& CCDestructor::globalLock() {{
	static thread_local bool ret = false;
	return ret; 
}}
bool& CCDestructor::lock(void* self) {
	return destructorLock()[self];
}
CCDestructor::~CCDestructor() {{
	destructorLock().erase(this);
}}

auto wrapFunction(uintptr_t address, tulip::hook::WrapperMetadata const& metadata) {
	auto wrapped = geode::hook::createWrapper(reinterpret_cast<void*>(address), metadata);
	if (wrapped.isErr()) {{
		throw std::runtime_error(wrapped.unwrapErr());
	}}
	return wrapped.unwrap();
}
)CAC";

	char const* declare_member = R"GEN(
auto {class_name}::{function_name}({parameters}){const} -> decltype({function_name}({arguments})) {{
	using FunctionType = decltype({function_name}({arguments}))(*)({class_name}{const}*{parameter_comma}{parameter_types});
	static auto func = wrapFunction(address<{addr_index}>(), tulip::hook::WrapperMetadata{{
		.m_convention = geode::hook::createConvention(tulip::hook::TulipConvention::{convention}),
		.m_abstract = tulip::hook::AbstractFunction::from(FunctionType(nullptr)),
	}});
	return reinterpret_cast<FunctionType>(func)(this{parameter_comma}{arguments});
}}
)GEN";

	char const* declare_virtual = R"GEN(
auto {class_name}::{function_name}({parameters}){const} -> decltype({function_name}({arguments})) {{
	auto self = addresser::thunkAdjust(Resolve<{parameter_types}>::func(&{class_name}::{function_name}), this);
	using FunctionType = decltype({function_name}({arguments}))(*)({class_name}{const}*{parameter_comma}{parameter_types});
	static auto func = wrapFunction(address<{addr_index}>(), tulip::hook::WrapperMetadata{{
		.m_convention = geode::hook::createConvention(tulip::hook::TulipConvention::{convention}),
		.m_abstract = tulip::hook::AbstractFunction::from(FunctionType(nullptr)),
	}});
	return reinterpret_cast<FunctionType>(func)(self{parameter_comma}{arguments});
}}
)GEN";

	char const* declare_static = R"GEN(
auto {class_name}::{function_name}({parameters}){const} -> decltype({function_name}({arguments})) {{
	using FunctionType = decltype({function_name}({arguments}))(*)({parameter_types});
	static auto func = wrapFunction(address<{addr_index}>(), tulip::hook::WrapperMetadata{{
		.m_convention = geode::hook::createConvention(tulip::hook::TulipConvention::{convention}),
		.m_abstract = tulip::hook::AbstractFunction::from(FunctionType(nullptr)),
	}});
	return reinterpret_cast<FunctionType>(func)({arguments});
}}
)GEN";

	char const* declare_destructor = R"GEN(
{class_name}::{function_name}({parameters}) {{
	// basically we destruct it once by calling the gd function, 
	// then lock it, so that other gd destructors dont get called
	if (CCDestructor::lock(this)) return;
	using FunctionType = void(*)({class_name}*{parameter_comma}{parameter_types});
	static auto func = wrapFunction(address<{addr_index}>(), tulip::hook::WrapperMetadata{{
		.m_convention = geode::hook::createConvention(tulip::hook::TulipConvention::{convention}),
		.m_abstract = tulip::hook::AbstractFunction::from(FunctionType(nullptr)),
	}});
	reinterpret_cast<FunctionType>(func)(this{parameter_comma}{arguments});
	// we need to construct it back so that it uhhh ummm doesnt crash
	// while going to the child destructors
	auto thing = new (this) {class_name}(geode::CutoffConstructor, sizeof({class_name}));
	CCDestructor::lock(this) = true;
}}
)GEN";

	// here we construct it as normal as we can, then destruct it
	// using the generated functions. this ensures no memory gets leaked
	// no crashes :pray:
	char const* declare_constructor = R"GEN(
{class_name}::{function_name}({parameters}) : {class_name}(geode::CutoffConstructor, sizeof({class_name})) {{
	CCDestructor::lock(this) = true;
	{class_name}::~{unqualified_class_name}();
	using FunctionType = void(*)({class_name}*{parameter_comma}{parameter_types});
	static auto func = wrapFunction(address<{addr_index}>(), tulip::hook::WrapperMetadata{{
		.m_convention = geode::hook::createConvention(tulip::hook::TulipConvention::{convention}),
		.m_abstract = tulip::hook::AbstractFunction::from(FunctionType(nullptr)),
	}});
	reinterpret_cast<FunctionType>(func)(this{parameter_comma}{arguments});
}}
)GEN";

	char const* ool_function_definition = R"GEN(
{return} {class_name}::{function_name}({parameters}){const} {definition}
)GEN";

	char const* ool_structor_function_definition = R"GEN(
{class_name}::{function_name}({parameters}){const} {definition}
)GEN";
}}

std::string generateAndroidStringBinding(Class&, FunctionBindField*);

std::string generateBindingSource(Root& root) {
	std::string output(format_strings::source_start);

	TypeBank bank;
	bank.loadFrom(root);

	for (auto& c : root.classes) {

		for (auto& f : c.fields) {
			if (auto i = f.get_as<InlineField>()) {
				if (codegen::platform == Platform::Mac || codegen::platform == Platform::iOS) {
					if (is_cocos_class(c.name))
						output += i->inner + "\n";
				}
			} else if (auto fn = f.get_as<OutOfLineField>()) {
				if (codegen::getStatus(f) != BindStatus::Unbindable)
					continue;
				
				// no cocos2d definitions on windows
				if (codegen::platform == Platform::Windows && is_cocos_class(f.parent)) {
					continue;
				}

				switch (fn->beginning.type) {
					case FunctionType::Ctor:
					case FunctionType::Dtor:
						output += fmt::format(format_strings::ool_structor_function_definition,
							fmt::arg("function_name", fn->beginning.name),
							fmt::arg("const", str_if(" const ", fn->beginning.is_const)),
							fmt::arg("class_name", c.name),
		                    fmt::arg("parameters", codegen::getParameters(fn->beginning)),
							fmt::arg("definition", fn->inner)
						);
						break;
					default:
						output += fmt::format(format_strings::ool_function_definition,
							fmt::arg("function_name", fn->beginning.name),
							fmt::arg("const", str_if(" const ", fn->beginning.is_const)),
							fmt::arg("class_name", c.name),
		                    fmt::arg("parameters", codegen::getParameters(fn->beginning)),
							fmt::arg("definition", fn->inner),
						    fmt::arg("return", fn->beginning.ret.name)
						);
						break;
				}
				
			} else if (auto fn = f.get_as<FunctionBindField>()) {
				if (codegen::getStatus(f) != BindStatus::NeedsBinding)
					continue;
				
				// no cocos2d definitions on windows
				if (codegen::platform == Platform::Windows && is_cocos_class(f.parent)) {
					continue;
				}

				if (codegen::platform == Platform::Android) {
					output += generateAndroidStringBinding(c, fn);
					continue;
				}

				char const* used_declare_format;

				switch (fn->beginning.type) {
					case FunctionType::Normal:
						used_declare_format = format_strings::declare_member;
						break;
					case FunctionType::Ctor:
						used_declare_format = format_strings::declare_constructor;
						break;
					case FunctionType::Dtor:
						used_declare_format = format_strings::declare_destructor;
						break;
				}

				if (fn->beginning.is_static)
					used_declare_format = format_strings::declare_static;
				if (fn->beginning.is_virtual && fn->beginning.type != FunctionType::Dtor)
					used_declare_format = format_strings::declare_virtual;

				auto ids = bank.getIDs(fn->beginning, c.name);

				output += fmt::format(used_declare_format,
					fmt::arg("class_name", c.name),
					fmt::arg("unqualified_class_name", codegen::getUnqualifiedClassName(c.name)),
					fmt::arg("const", str_if(" const ", fn->beginning.is_const)),
					fmt::arg("convention", codegen::getModifyConventionName(f)),
					fmt::arg("function_name", fn->beginning.name),
					fmt::arg("meta_index", ids.meta),
					fmt::arg("member_index", ids.member),
					fmt::arg("ret_index", ids.ret),
					fmt::arg("addr_index", f.field_id),
					fmt::arg("parameters", codegen::getParameters(fn->beginning)),
					fmt::arg("parameter_types", codegen::getParameterTypes(fn->beginning)),
					fmt::arg("arguments", codegen::getParameterNames(fn->beginning)),
					fmt::arg("parameter_comma", str_if(", ", !fn->beginning.args.empty()))
				);
			}
		}
	}
	return output;
}

#include <unordered_set>

std::string mangle_name(std::string_view str) {
	if (str.find("::") != -1) {
		std::string result = "N";
		auto s = str;
		do {
			const auto i = s.find("::");
			const auto t = s.substr(0, i);
			result += std::to_string(t.size()) + std::string(t);
			if (i == -1) s = "";
			else
				s = s.substr(i + 2);
		} while(s.size());
		return result + "E";
	} else {
		return std::to_string(str.size()) + std::string(str);
	}
};

std::string int_to_string(unsigned int value, unsigned int radix) {
    static constexpr char base36[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result;
    do {
        unsigned int remainder = value % radix;
        value /= radix;
        result.insert(result.begin(), base36[remainder]);
    } while (value);
    return result;
}

std::string have_i_seen_it_before(std::vector<std::string>& seen, std::string mangled) {
	for (int i = 0; i < seen.size(); ++i) {
		if (seen[i] == mangled) {
			if (i == 0) return "S_";
			// yes, its base 36
			return "S" + int_to_string(i - 1, 36) + "_";
		}
	}
	return "";
}

std::string substitute_seen(std::vector<std::string>& seen, std::string mangled, bool subs) {
	if (!subs) return mangled;
    if (mangled.empty()) return mangled;
	if (auto x = have_i_seen_it_before(seen, mangled); !x.empty()) return x;
	seen.push_back(mangled);
	return mangled;
}

std::string mangle_type(std::vector<std::string>& seen, std::string name, bool subs = true) {
	if (name == "int") return "i";
	if (name == "float") return "f";
	if (name == "bool") return "b";
	if (name == "char") return "c";
	if (name == "gd::string") return "Ss";
	if (name == "cocos2d::ccColor3B") return mangle_type(seen, "cocos2d::_ccColor3B", subs);
	// too lazy
	if (name == "gd::map<gd::string, gd::string>") return "St3mapISsSsSt4lessISsESaISt4pairIKSsSsEEE";
	if (name == "cocos2d::SEL_MenuHandler") {
		const auto a = mangle_type(seen, "cocos2d::CCObject", subs);
		const auto b = mangle_type(seen, "cocos2d::CCObject*", subs);
		const auto fnptr = substitute_seen(seen, "Fv" + b + "E", subs);
		return substitute_seen(seen, "M" + a + fnptr, subs);
	}
	if (name.find('*') == name.size() - 1) {
		auto inner = mangle_type(seen, name.substr(0, name.size() - 1), false);
		if (auto x = have_i_seen_it_before(seen, "P" + inner); !x.empty()) return x;
		inner = mangle_type(seen, name.substr(0, name.size() - 1), subs);
		return substitute_seen(seen, "P" + inner, subs);
	}
	if (name.find('&') == name.size() - 1) {
		auto inner = mangle_type(seen, name.substr(0, name.size() - 1), false);
		if (auto x = have_i_seen_it_before(seen, "R" + inner); !x.empty()) return x;
		inner = mangle_type(seen, name.substr(0, name.size() - 1), subs);
		return substitute_seen(seen, "R" + inner, subs);
	}
	if (auto i = name.find("const"); i != -1) {
		std::string inner;
		// at the end of the name
		if (i == name.size() - 5) {
			inner = mangle_type(seen, name.substr(0, i - 1));
		} else if (i == 0) {
			inner = mangle_type(seen, name.substr(6));
		} else {
			inner = "v";
			std::cout << "um " << name << std::endl;
		}
		return substitute_seen(seen, "K" + inner, subs);
	}

	// static std::unordered_set<std::string> seen_types;
	// if (seen_types.count(name) == 0) {
	// 	std::cout << "|" << name << "|" << std::endl;
	// 	seen_types.insert(name);
	// }

	if (name.find("::") != -1) {
		std::string result = "";
		std::string substituted = "";
		auto s = name;
		do {
			const auto i = s.find("::");
			const auto t = s.substr(0, i);
            auto part = std::to_string(t.size()) + std::string(t);
		    if (auto x = have_i_seen_it_before(seen, result + part); !x.empty()) {
                substituted = x;
            } else {
                substituted = substitute_seen(seen, substituted + part, subs);
            }
            result += part;

			if (i == -1) s = "";
			else s = s.substr(i + 2);
		} while(s.size());
        if (substituted.size() == 3 && substituted[0] == 'S')
            return substituted;
		return "N" + substituted + "E";
	} else {
		return substitute_seen(seen, mangle_name(name), subs);
	}
};

std::string generateAndroidStringBinding(Class& clazz, FunctionBindField* fn) {
	// if (fn->beginning.is_virtual || fn->beginning.is_static) {
	// 	return "poob\n";
	// }
	auto format_str = R"GEN(
auto {class_name}::{function_name}({parameters}) -> decltype({function_name}({arguments})) {{
	using FunctionType = decltype({function_name}({arguments}))(*)({class_name}*{parameter_comma}{parameter_types});

	void* const addr = dlsym(reinterpret_cast<void*>(geode::base::get()), "{mangled_symbol}");

	assert(addr != nullptr);

	return reinterpret_cast<FunctionType>(addr)(this{parameter_comma}{arguments});
}}
)GEN";

	if (fn->beginning.is_static) {
		format_str = R"GEN(
auto {class_name}::{function_name}({parameters}){const} -> decltype({function_name}({arguments})) {{
	using FunctionType = decltype({function_name}({arguments}))(*)({parameter_types});
	
	void* const addr = dlsym(reinterpret_cast<void*>(geode::base::get()), "{mangled_symbol}");

	assert(addr != nullptr);

	return reinterpret_cast<FunctionType>(addr)({arguments});
}}
)GEN";
	}

	auto& decl = fn->beginning;

	std::string mangled_symbol = "_Z" + mangle_name(clazz.name + "::" + decl.name);
	if (decl.args.empty()) {
		mangled_symbol += "v";
	} else {
		std::vector<std::string> seen;
		static constexpr auto first_bit = [](std::string_view str, std::string_view sep) {
			return str.substr(0, str.find(sep));
		};
		// this is S_
		seen.push_back(mangle_name(first_bit(clazz.name, "::")));
		for (auto& [ty, _] : decl.args) {
			mangled_symbol += mangle_type(seen, ty.name);
		}
		// std::cout << mangled_symbol << "\n";
		// std::cout << "seen: [";
		// bool first = true;
		// for (auto& a : seen) {
		// 	if (!first) {
		// 		std::cout << ", ";
		// 	}
		// 	first = false;
		// 	std::cout << "\"" << a << "\"";
		// }
		// std::cout << "]" << std::endl;
	}

	return fmt::format(
		format_str,
		fmt::arg("class_name", clazz.name),
		fmt::arg("const", str_if(" const ", fn->beginning.is_const)),
		fmt::arg("function_name", fn->beginning.name),
		fmt::arg("parameters", codegen::getParameters(fn->beginning)),
		fmt::arg("parameter_types", codegen::getParameterTypes(fn->beginning)),
		fmt::arg("arguments", codegen::getParameterNames(fn->beginning)),
		fmt::arg("parameter_comma", str_if(", ", !fn->beginning.args.empty())),

		fmt::arg("mangled_symbol", mangled_symbol)
	);
}