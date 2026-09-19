#version 330

// Live background blur behind in-game inventory screens (ui::
// begin_blurred_background()/end_blurred_background()): a 3x3 Gaussian
// (1 2 1 / 2 4 2 / 1 2 1) over a half-resolution render of the world,
// drawn stretched back up with bilinear filtering - the same soft look the
// old one-off CPU capture had (half-size + ImageBlurGaussian radius 1), but
// cheap enough to run every frame. Used with raylib's default vertex shader.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 texelSize; // 1 / source texture size

out vec4 finalColor;

void main()
{
    vec4 sum = vec4(0.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
            sum += texture(texture0, fragTexCoord + vec2(x, y) * texelSize) * weight;
        }
    }
    finalColor = (sum / 16.0) * colDiffuse * fragColor;
}
