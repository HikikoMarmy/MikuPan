#version 330 core
out vec4 FragColor;

in vec4 outColor;

void main()
{
    if (outColor.a <= 0.0f)
    {
        discard;
    }

    FragColor = outColor;
}
