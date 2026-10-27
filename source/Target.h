#pragma once

#include <FFGLSDK.h>

namespace isohypse
{
/**
    An off-screen target: one or two colour textures and a framebuffer.

    The fleet's PassBuffer, widened for what this plugin draws into: two
    attachments for the map pass (the picture, and the light the glow
    spreads), linear filtering for the glow's quarter-raster passes, mipmaps
    for a heightfield the map pass samples at any scale, and wrapping in x
    for a world whose longitude goes round.

    What it keeps: it reallocates only when something changed; a new target
    is cleared; `Destroy()` deletes everything; and nothing here uses an
    `ffglex::Scoped*` binding (they clear to 0 on exit instead of restoring),
    so it saves and puts back the texture, the framebuffer and the viewport
    by hand.
*/
class Target
{
public:
	struct Spec
	{
		GLint format    = GL_RGBA16F;
		int attachments = 1;
		bool linear     = false;
		bool mipmapped  = false;
		bool wrapX      = false;
		bool operator==( const Spec& o ) const
		{
			return format == o.format && attachments == o.attachments && linear == o.linear && mipmapped == o.mipmapped
			       && wrapX == o.wrapX;
		}
	};

	/// Allocate, reusing what exists if it already matches. False if the
	/// framebuffer is incomplete.
	bool Ensure( int width, int height, const Spec& spec );
	void Destroy();

	/// Bind the framebuffer (every attachment drawn) and cover it.
	void Bind() const;
	/// Rebuild the mip chain after drawing level 0.
	void GenerateMips() const;
	void Clear() const;

	GLuint Texture( int attachment = 0 ) const
	{
		return textures[ attachment ];
	}
	int Width() const
	{
		return width;
	}
	int Height() const
	{
		return height;
	}
	/// The index of the smallest mip level (0 without mipmaps).
	int TopLevel() const
	{
		return topLevel;
	}
	bool IsValid() const
	{
		return framebuffer != 0;
	}

private:
	GLuint textures[ 2 ] = { 0, 0 };
	GLuint framebuffer   = 0;
	int width = 0, height = 0, topLevel = 0;
	Spec spec;
};

/// The number of the smallest mip level of a width x height texture.
int TopMipLevel( int width, int height );

/// A static texture's sampling: linear, mipmapped, and wrapping in x or not.
void SetHeightfieldSampling( GLuint texture, bool wrapX );

} // namespace isohypse
