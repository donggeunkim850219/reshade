#pragma once

#include "Lexer.hpp"
#include <vector>
#include <list>
#include <algorithm>

namespace ReShade
{
	namespace FX
	{
		enum class nodeid
		{
			unknown,

			LValue,
			Literal,
			Unary,
			Binary,
			Intrinsic,
			Conditional,
			Assignment,
			Sequence,
			Call,
			Constructor,
			Swizzle,
			FieldSelection,
			InitializerList,

			Compound,
			DeclaratorList,
			ExpressionStatement,
			If,
			Switch,
			Case,
			For,
			While,
			Return,
			Jump,

			Annotation,
			Variable,
			Struct,
			Function,
			Pass,
			Technique
		};
		class node abstract
		{
			void operator=(const node &);

		public:
			const nodeid id;
			location location;
			
		protected:
			explicit node(nodeid id) : id(id), location()
			{
			}
		};
		class nodetree
		{
		public:
			template <typename T>
			T *make_node(const location &location)
			{
				const auto node = _pool.add<T>();
				node->location = location;

				return node;
			}

			std::vector<node *> structs, uniforms, functions, techniques;

		private:
			class memory_pool
			{
			public:
				~memory_pool()
				{
					clear();
				}

				template <typename T>
				T *add()
				{
					auto size = sizeof(nodeinfo) - sizeof(node) + sizeof(T);
					auto page = std::find_if(_pages.begin(), _pages.end(),
						[size](const struct page &page)
						{
							return page.cursor + size < page.memory.size();
						});

					if (page == _pages.end())
					{
						_pages.emplace_back(std::max(size_t(4096), size));

						page = std::prev(_pages.end());
					}

					const auto node = new (&page->memory.at(page->cursor)) nodeinfo;
					const auto node_data = new (&node->data) T();
					node->size = size;
					node->dtor = [](void *object) { reinterpret_cast<T *>(object)->~T(); };

					page->cursor += node->size;

					return node_data;
				}
				void clear()
				{
					for (auto &page : _pages)
					{
						auto node = reinterpret_cast<nodeinfo *>(&page.memory.front());

						do
						{
							node->dtor(node->data);
							node = reinterpret_cast<nodeinfo *>(reinterpret_cast<unsigned char *>(node) + node->size);
						}
						while (node->size > 0 && (page.cursor -= node->size) > 0);
					}
				}

			private:
				struct page
				{
					explicit page(size_t size) : cursor(0), memory(size, 0)
					{
					}

					size_t cursor;
					std::vector<unsigned char> memory;
				};
				struct nodeinfo
				{
					size_t size;
					void(*dtor)(void *);
					unsigned char data[sizeof(node)];
				};

				std::list<page> _pages;
			} _pool;
		};

		namespace Nodes
		{
			struct Type
			{
				enum class Class
				{
					Void,
					Bool,
					Int,
					Uint,
					Float,
					Sampler1D,
					Sampler2D,
					Sampler3D,
					Texture1D,
					Texture2D,
					Texture3D,
					Struct,
					String
				};
				enum Qualifier
				{
					// Storage
					Extern = 1 << 0,
					Static = 1 << 1,
					Uniform = 1 << 2,
					Volatile = 1 << 3,
					Precise = 1 << 4,
					In = 1 << 5,
					Out = 1 << 6,
					InOut = In | Out,

					// Modifier
					Const = 1 << 8,

					// Interpolation
					Linear = 1 << 10,
					NoPerspective = 1 << 11,
					Centroid = 1 << 12,
					NoInterpolation = 1 << 13,
				};

				inline bool IsArray() const
				{
					return ArrayLength != 0;
				}
				inline bool IsMatrix() const
				{
					return Rows >= 1 && Cols > 1;
				}
				inline bool IsVector() const
				{
					return Rows > 1 && !IsMatrix();
				}
				inline bool IsScalar() const
				{
					return !IsArray() && !IsMatrix() && !IsVector() && IsNumeric();
				}
				inline bool IsNumeric() const
				{
					return IsBoolean() || IsIntegral() || IsFloatingPoint();
				}
				inline bool IsVoid() const
				{
					return BaseClass == Class::Void;
				}
				inline bool IsBoolean() const
				{
					return BaseClass == Class::Bool;
				}
				inline bool IsIntegral() const
				{
					return BaseClass == Class::Int || BaseClass == Class::Uint;
				}
				inline bool IsFloatingPoint() const
				{
					return BaseClass == Class::Float;
				}
				inline bool IsTexture() const
				{
					return BaseClass >= Class::Texture1D && BaseClass <= Class::Texture2D;
				}
				inline bool IsSampler() const
				{
					return BaseClass >= Class::Sampler1D && BaseClass <= Class::Sampler3D;
				}
				inline bool IsStruct() const
				{
					return BaseClass == Class::Struct;
				}
				inline bool HasQualifier(Qualifier qualifier) const
				{
					return (Qualifiers & static_cast<unsigned int>(qualifier)) == static_cast<unsigned int>(qualifier);
				}

				Class BaseClass;
				unsigned int Qualifiers;
				unsigned int Rows : 4, Cols : 4;
				int ArrayLength;
				struct Struct *Definition;
			};
	
			struct Expression abstract : public node
			{
				Type Type;

			protected:
				Expression(nodeid id) : node(id)
				{
				}
			};
			struct Statement abstract : public node
			{
				std::vector<std::string> Attributes;

			protected:
				Statement(nodeid id) : node(id)
				{
				}
			};
			struct Declaration abstract : public node
			{
				std::string Name, Namespace;

			protected:
				Declaration(nodeid id) : node(id)
				{
				}
			};

			// Expressions
			struct LValue : public Expression
			{
				const struct Variable *Reference;

				LValue() : Expression(nodeid::LValue), Reference(nullptr)
				{
				}
			};
			struct Literal : public Expression
			{
				union Value
				{
					int Int[16];
					unsigned int Uint[16];
					float Float[16];
				};

				Value Value;
				std::string StringValue;

				Literal() : Expression(nodeid::Literal)
				{
					memset(&Value, 0, sizeof(Value));
				}
			};
			struct Unary : public Expression
			{
				enum class Op
				{
					None,

					Negate,
					BitwiseNot,
					LogicalNot,
					Increase,
					Decrease,
					PostIncrease,
					PostDecrease,
					Cast,
				};

				Op Operator;
				Expression *Operand;

				Unary() : Expression(nodeid::Unary), Operator(Op::None), Operand(nullptr)
				{
				}
			};
			struct Binary : public Expression
			{
				enum class Op
				{
					None,

					Add,
					Subtract,
					Multiply,
					Divide,
					Modulo,
					Less,
					Greater,
					LessOrEqual,
					GreaterOrEqual,
					Equal,
					NotEqual,
					LeftShift,
					RightShift,
					BitwiseOr,
					BitwiseXor,
					BitwiseAnd,
					LogicalOr,
					LogicalAnd,
					ElementExtract
				};

				Op Operator;
				Expression *Operands[2];

				Binary() : Expression(nodeid::Binary), Operator(Op::None), Operands()
				{
				}
			};
			struct Intrinsic : public Expression
			{
				enum class Op
				{
					None,

					Abs,
					Acos,
					All,
					Any,
					BitCastInt2Float,
					BitCastUint2Float,
					Asin,
					BitCastFloat2Int,
					BitCastFloat2Uint,
					Atan,
					Atan2,
					Ceil,
					Clamp,
					Cos,
					Cosh,
					Cross,
					PartialDerivativeX,
					PartialDerivativeY,
					Degrees,
					Determinant,
					Distance,
					Dot,
					Exp,
					Exp2,
					FaceForward,
					Floor,
					Frac,
					Frexp,
					Fwidth,
					Ldexp,
					Length,
					Lerp,
					Log,
					Log10,
					Log2,
					Mad,
					Max,
					Min,
					Modf,
					Mul,
					Normalize,
					Pow,
					Radians,
					Rcp,
					Reflect,
					Refract,
					Round,
					Rsqrt,
					Saturate,
					Sign,
					Sin,
					SinCos,
					Sinh,
					SmoothStep,
					Sqrt,
					Step,
					Tan,
					Tanh,
					Tex2D,
					Tex2DFetch,
					Tex2DGather,
					Tex2DGatherOffset,
					Tex2DGrad,
					Tex2DLevel,
					Tex2DLevelOffset,
					Tex2DOffset,
					Tex2DProj,
					Tex2DSize,
					Transpose,
					Trunc,
				};

				Op Operator;
				Expression *Arguments[4];

				Intrinsic() : Expression(nodeid::Intrinsic), Operator(Op::None), Arguments()
				{
				}
			};
			struct Conditional : public Expression
			{
				Expression *Condition;
				Expression *ExpressionOnTrue, *ExpressionOnFalse;

				Conditional() : Expression(nodeid::Conditional), Condition(nullptr), ExpressionOnTrue(nullptr), ExpressionOnFalse(nullptr)
				{
				}
			};
			struct Assignment : public Expression
			{
				enum class Op
				{
					None,
					Add,
					Subtract,
					Multiply,
					Divide,
					Modulo,
					BitwiseAnd,
					BitwiseOr,
					BitwiseXor,
					LeftShift,
					RightShift
				};

				Op Operator;
				Expression *Left, *Right;

				Assignment() : Expression(nodeid::Assignment), Operator(Op::None)
				{
				}
			};
			struct Sequence : public Expression
			{
				std::vector<Expression *> Expressions;

				Sequence() : Expression(nodeid::Sequence)
				{
				}
			};
			struct Call : public Expression
			{
				std::string CalleeName;
				const struct Function *Callee;
				std::vector<Expression *> Arguments;

				Call() : Expression(nodeid::Call), Callee(nullptr)
				{
				}
			};
			struct Constructor : public Expression
			{
				std::vector<Expression *> Arguments;

				Constructor() : Expression(nodeid::Constructor)
				{
				}
			};
			struct Swizzle : public Expression
			{
				Expression *Operand;
				signed char Mask[4];

				Swizzle() : Expression(nodeid::Swizzle), Operand(nullptr)
				{
					Mask[0] = Mask[1] = Mask[2] = Mask[3] = -1;
				}
			};
			struct FieldSelection : public Expression
			{
				Expression *Operand;
				Variable *Field;

				FieldSelection() : Expression(nodeid::FieldSelection), Operand(nullptr), Field(nullptr)
				{
				}
			};
			struct InitializerList : public Expression
			{
				std::vector<Expression *> Values;

				InitializerList() : Expression(nodeid::InitializerList)
				{
				}
			};

			// Statements
			struct Compound : public Statement
			{
				std::vector<Statement *> Statements;

				Compound() : Statement(nodeid::Compound)
				{
				}
			};
			struct DeclaratorList : public Statement
			{
				std::vector<struct Variable *> Declarators;

				DeclaratorList() : Statement(nodeid::DeclaratorList)
				{
				}
			};
			struct ExpressionStatement : public Statement
			{
				Expression *Expression;

				ExpressionStatement() : Statement(nodeid::ExpressionStatement)
				{
				}
			};
			struct If : public Statement
			{
				Expression *Condition;
				Statement *StatementOnTrue, *StatementOnFalse;
				
				If() : Statement(nodeid::If)
				{
				}
			};
			struct Case : public Statement
			{
				std::vector<struct Literal *> Labels;
				Statement *Statements;

				Case() : Statement(nodeid::Case)
				{
				}
			};
			struct Switch : public Statement
			{
				Expression *Test;
				std::vector<Case *> Cases;

				Switch() : Statement(nodeid::Switch)
				{
				}
			};
			struct For : public Statement
			{
				Statement *Initialization;
				Expression *Condition, *Increment;
				Statement *Statements;
				
				For() : Statement(nodeid::For)
				{
				}
			};
			struct While : public Statement
			{
				bool DoWhile;
				Expression *Condition;
				Statement *Statements;
				
				While() : Statement(nodeid::While), DoWhile(false)
				{
				}
			};
			struct Return : public Statement
			{
				bool Discard;
				Expression *Value;
				
				Return() : Statement(nodeid::Return), Discard(false), Value(nullptr)
				{
				}
			};
			struct Jump : public Statement
			{
				enum Mode
				{
					Break,
					Continue
				};

				Mode Mode;
				
				Jump() : Statement(nodeid::Jump), Mode(Break)
				{
				}
			};

			// Declarations
			struct Annotation : public node
			{
				std::string Name;
				Literal *Value;
				
				Annotation() : node(nodeid::Annotation)
				{
				}
			};
			struct Variable : public Declaration
			{
				struct Properties
				{
					enum : unsigned int
					{
						NONE = 0,

						R8 = 50,
						R16F = 111,
						R32F = 114,
						RG8 = 51,
						RG16 = 34,
						RG16F = 112,
						RG32F = 115,
						RGBA8 = 32,
						RGBA16 = 36,
						RGBA16F = 113,
						RGBA32F = 116,
						DXT1 = 827611204,
						DXT3 = 861165636,
						DXT5 = 894720068,
						LATC1 = 826889281,
						LATC2 = 843666497,

						POINT = 1,
						LINEAR,
						ANISOTROPIC,

						WRAP = 1,
						REPEAT = 1,
						MIRROR,
						CLAMP,
						BORDER,
					};

					Properties() : Texture(nullptr), Width(1), Height(1), Depth(1), MipLevels(1), Format(RGBA8), SRGBTexture(false), AddressU(CLAMP), AddressV(CLAMP), AddressW(CLAMP), MinFilter(LINEAR), MagFilter(LINEAR), MipFilter(LINEAR), MaxAnisotropy(1), MinLOD(0), MaxLOD(FLT_MAX), MipLODBias(0.0f)
					{
					}

					const Variable *Texture;
					unsigned int Width, Height, Depth, MipLevels;
					unsigned int Format;
					bool SRGBTexture;
					unsigned int AddressU, AddressV, AddressW, MinFilter, MagFilter, MipFilter;
					unsigned int MaxAnisotropy;
					float MinLOD, MaxLOD, MipLODBias;
				};

				Type Type;
				std::vector<Annotation> Annotations;
				std::string Semantic;
				Properties Properties;
				Expression *Initializer;
				
				Variable() : Declaration(nodeid::Variable), Initializer(nullptr)
				{
				}
			};
			struct Struct : public Declaration
			{
				std::vector<Variable *> Fields;
				
				Struct() : Declaration(nodeid::Struct)
				{
				}
			};
			struct Function : public Declaration
			{
				Type ReturnType;
				std::vector<Variable *> Parameters;
				std::string ReturnSemantic;
				Compound *Definition;
				
				Function() : Declaration(nodeid::Function), Definition(nullptr)
				{
				}
			};
			struct Pass : public Declaration
			{
				struct States
				{
					enum : unsigned int
					{
						NONE = 0,

						ZERO = 0,
						ONE = 1,
						SRCCOLOR,
						INVSRCCOLOR,
						SRCALPHA,
						INVSRCALPHA,
						DESTALPHA,
						INVDESTALPHA,
						DESTCOLOR,
						INVDESTCOLOR,

						ADD = 1,
						SUBTRACT,
						REVSUBTRACT,
						MIN,
						MAX,

						KEEP = 1,
						REPLACE = 3,
						INVERT,
						INCRSAT,
						DECRSAT,
						INCR,
						DECR,

						NEVER = 1,
						LESS,
						EQUAL,
						LESSEQUAL,
						GREATER,
						NOTEQUAL,
						GREATEREQUAL,
						ALWAYS
					};

					States() : RenderTargets(), VertexShader(nullptr), PixelShader(nullptr), SRGBWriteEnable(false), BlendEnable(false), DepthEnable(false), StencilEnable(false), RenderTargetWriteMask(0xF), DepthWriteMask(1), StencilReadMask(0xFF), StencilWriteMask(0xFF), BlendOp(ADD), BlendOpAlpha(ADD), SrcBlend(ONE), DestBlend(ZERO), DepthFunc(LESS), StencilFunc(ALWAYS), StencilRef(0), StencilOpPass(KEEP), StencilOpFail(KEEP), StencilOpDepthFail(KEEP)
					{
					}

					const Variable *RenderTargets[8];
					const Function *VertexShader, *PixelShader;
					bool SRGBWriteEnable, BlendEnable, DepthEnable, StencilEnable;
					unsigned char RenderTargetWriteMask, DepthWriteMask, StencilReadMask, StencilWriteMask;
					unsigned int BlendOp, BlendOpAlpha, SrcBlend, DestBlend, DepthFunc, StencilFunc, StencilRef, StencilOpPass, StencilOpFail, StencilOpDepthFail;
				};

				std::vector<Annotation> Annotations;
				States States;
				
				Pass() : Declaration(nodeid::Pass)
				{
				}
			};
			struct Technique : public Declaration
			{
				std::vector<Annotation> Annotations;
				std::vector<Pass *> Passes;
				
				Technique() : Declaration(nodeid::Technique)
				{
				}
			};
		}
	}
}